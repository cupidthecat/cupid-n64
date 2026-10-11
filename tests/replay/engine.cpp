#include "engine.hpp"
#include "core/cpu/fpu/environment.hpp"
#include <algorithm>
#include <bit>
#include <iostream>

namespace test::machine_replay {
using namespace cupid::n64;
namespace {

enum class OutputKind : unsigned { AudioSample, AudioRate, VideoFrame, RdpCommands, RdpSync };

struct OutputEvent {
  OutputKind kind;
  std::uint64_t clocks;
  std::vector<std::uint64_t> payload;
};

std::string_view output_name(OutputKind kind) {
  switch (kind) {
  case OutputKind::AudioSample:
    return "audio.sample";
  case OutputKind::AudioRate:
    return "audio.rate";
  case OutputKind::VideoFrame:
    return "video.frame";
  case OutputKind::RdpCommands:
    return "rdp.commands";
  case OutputKind::RdpSync:
    return "rdp.sync";
  }
  throw state::InvalidState("Unknown output event");
}

ConsoleConfig config(const Input &input) {
  ConsoleConfig value;
  value.region = input.region;
  value.expansion = input.expansion;
  value.random_seed = input.seed;
  value.eeprom_size = 512;
  value.rtc_present = true;
  value.rtc_clock = [] { return std::int64_t(946684800); };
  value.disk_clock = value.rtc_clock;
  return value;
}

struct Machine {
  Console console;
  std::vector<OutputEvent> output;

  explicit Machine(const Input &input) : console(config(input)) {
    std::array<std::uint8_t, 0x7c0> firmware{};
    std::array<std::uint8_t, 4096> cartridge{};
    cartridge[0] = 0x80;
    cartridge[1] = 0x37;
    cartridge[2] = 0x12;
    cartridge[3] = 0x40;
    state::Archive::require(console.load(cartridge, firmware));
    console.write(0x04700008, 4, 0);
    console.write(0x0470000c, 4, 0x14);
    console.write(0x04300000, 4, 0x10f);
    console.write(0x03f80008, 4, 0x00080008);
    const auto chips = console.ram().size() / 0x200000;
    for (unsigned chip = 0; chip < chips; ++chip) {
      console.write(0x03f0000c, 4, 0x02000000);
      console.write(0x03f00004, 4, (chip + 4) * 2 << 26);
    }
    for (unsigned chip = 0; chip < chips; ++chip)
      console.write(0x03f00004 + (chip + 4) * 0x800, 4, chip * 2 << 26);
    console.cpu().write_control(Status, 0x34000000);
    console.cpu().write_control(Compare, 0x10000000);
    console.cpu().set_pc(0xffffffff80001000);
    console.cpu().state().gpr[31] = 0xffffffff80003000;
    console.connect_controller(0, true);
    console.controller(0).memory_pak();
    console.connect_mouse(1);
    console.connect_gamecube_controller(2);
    console.audio().connect(
        [this](StereoSample sample) {
          output.push_back({OutputKind::AudioSample,
                            console.cpu().state().clocks,
                            {std::bit_cast<std::uint64_t>(sample.left),
                             std::bit_cast<std::uint64_t>(sample.right)}});
        },
        [this](unsigned rate) {
          output.push_back({OutputKind::AudioRate, console.cpu().state().clocks, {rate}});
        });
    console.video().connect_frame([this](bool field) {
      output.push_back({OutputKind::VideoFrame, console.cpu().state().clocks, {unsigned(field)}});
    });
    console.display().connect(
        [this](std::span<const std::uint32_t> words) {
          output.push_back({OutputKind::RdpCommands,
                            console.cpu().state().clocks,
                            {words.begin(), words.end()}});
        },
        [this] { output.push_back({OutputKind::RdpSync, console.cpu().state().clocks, {}}); });
  }

  void apply(const Action &action, Mode mode, std::int64_t signal_clock_target = 0) {
    floating::Environment environment(0);
    auto &cpu = console.cpu();
    auto &rsp = console.signal();
    const auto signal = mode == Mode::Native ? Rsp::Execution::Native : Rsp::Execution::Interpreted;
    switch (action.kind) {
    case ActionKind::CpuBlock: {
      // A complete block may exceed its deadline; a self-loop adds 126 clocks per branch.
      const auto target = cpu.state().clocks + action.value;
      const bool ran =
          mode == Mode::Native ? cpu.run_block(target) : cpu.run_interpreted_block(target);
      if (!ran)
        cpu.step();
      console.synchronize(signal, signal_clock_target);
      break;
    }
    case ActionKind::CpuSteps:
      // Each instruction fetch charges its clocks before decode and synchronizes devices once.
      for (std::uint64_t n = 0; n < action.value; ++n) {
        cpu.step();
        console.synchronize(signal, signal_clock_target);
      }
      break;
    case ActionKind::RspRun:
      rsp.elapse(static_cast<std::uint32_t>(action.value));
      if (mode == Mode::Native)
        rsp.run();
      else
        rsp.run_interpreted(signal_clock_target);
      break;
    case ActionKind::WriteMemory:
      console.write(action.address, action.bytes, action.value);
      break;
    case ActionKind::CpuRegister:
      cpu.state().gpr[action.address] = action.value;
      break;
    case ActionKind::CpuControl:
      cpu.write_control(action.address, action.value);
      break;
    case ActionKind::CpuFloating:
      cpu.state().fpr[action.address] = action.value;
      break;
    case ActionKind::CpuInstruction:
      cpu.execute(static_cast<std::uint32_t>(action.value));
      break;
    case ActionKind::CpuPc:
      cpu.set_pc(action.value);
      break;
    case ActionKind::RspRegister:
      rsp.state().gpr[action.address] = static_cast<std::uint32_t>(action.value);
      break;
    case ActionKind::RspVector:
      rsp.state().vectors[action.address / 8].lanes[action.address % 8] =
          static_cast<std::uint16_t>(action.value);
      break;
    case ActionKind::RspLocal:
      rsp.write_local(action.address, action.bytes, action.value);
      break;
    case ActionKind::RspIo:
      rsp.write_io(action.address, static_cast<std::uint32_t>(action.value));
      break;
    case ActionKind::RspPc:
      rsp.write_status(0, static_cast<std::uint32_t>(action.value));
      break;
    case ActionKind::Controller:
      console.controller(action.address)
          .input(static_cast<std::uint16_t>(action.value),
                 std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(action.value >> 16)),
                 std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(action.value >> 24)));
      break;
    case ActionKind::AdvanceClocks:
      cpu.advance_clocks(action.value);
      console.synchronize(signal, signal_clock_target);
      break;
    }
  }
};

StateCheckpoint guest_checkpoint(Console &console) {
  auto checkpoint = CoreState::checkpoint(console);
  // Compiled code and watched RAM ranges describe host execution history, not guest state.
  const auto history =
      std::find_if(checkpoint.ranges.begin(), checkpoint.ranges.end(),
                   [](const auto &range) { return range.name == "rsp.execution_history"; });
  state::Archive::require(history != checkpoint.ranges.end());
  checkpoint.bytes.resize(history->offset + 8);
  checkpoint.ranges.erase(history, checkpoint.ranges.end());
  // This scratch counter accumulates a native batch; the scalar scheduler stores its last issue.
  const auto scratch =
      std::find_if(checkpoint.ranges.begin(), checkpoint.ranges.end(),
                   [](const auto &range) { return range.name == "rsp.pipeline.clocks"; });
  state::Archive::require(scratch != checkpoint.ranges.end());
  std::fill_n(checkpoint.bytes.begin() + scratch->offset, scratch->bytes, 0);
  const auto checksum =
      state::Archive::fingerprint(std::span(checkpoint.bytes).first(checkpoint.bytes.size() - 8));
  for (unsigned byte = 0; byte < 8; ++byte)
    checkpoint.bytes[checkpoint.bytes.size() - 8 + byte] =
        static_cast<std::uint8_t>(checksum >> (byte * 8));
  return checkpoint;
}

StateCheckpoint output_checkpoint(const Machine &machine) {
  state::Archive archive(true);
  archive.label("output.events.size", 4);
  archive.literal(static_cast<std::uint32_t>(machine.output.size()));
  for (std::size_t n = 0; n < machine.output.size(); ++n) {
    const auto &event = machine.output[n];
    archive.indexed_label("output", n, "kind", 4);
    archive.literal(event.kind);
    const auto name = std::string("output.") + std::string(output_name(event.kind)) + "[" +
                      std::to_string(n) + "].";
    archive.label(name + "clocks", 8);
    archive.literal(event.clocks);
    archive.label(name + "size", 4);
    archive.literal(static_cast<std::uint32_t>(event.payload.size()));
    for (std::size_t element = 0; element < event.payload.size(); ++element) {
      const auto field = event.kind == OutputKind::AudioSample  ? (element == 0 ? "left" : "right")
                         : event.kind == OutputKind::AudioRate  ? "rate"
                         : event.kind == OutputKind::VideoFrame ? "field"
                                                                : "word";
      archive.label(
          name + field +
              (event.kind == OutputKind::RdpCommands ? "[" + std::to_string(element) + "]" : ""),
          8);
      archive.literal(event.payload[element]);
    }
  }
  auto ranges = archive.ranges();
  return {archive.finish(), std::move(ranges)};
}

} // namespace

std::optional<Difference> compare(const Input &input, const Observer &observe) {
  const auto &contracts = input.contracts;
  for (const auto &contract : contracts)
    state::Archive::require(contract.boundary <= input.actions.size() && contract.state.bytes);
  Machine expected(input), actual(input);
  for (const auto &action : input.initial) {
    state::Archive::require(valid(action, true));
    expected.apply(action, Mode::Interpreted);
  }
  const auto start = CoreState::capture(expected.console);
  CoreState::restore(actual.console, start);
  expected.output.clear();
  actual.output.clear();
  const auto checkpoint = [&](std::size_t boundary) -> std::optional<StateDifference> {
    std::vector<bool> observed(contracts.size());
    const auto compare_state = [&](StateCheckpoint left,
                                   const StateCheckpoint &right) -> std::optional<StateDifference> {
      while (const auto state = left.difference(right)) {
        const auto contract =
            std::find_if(contracts.begin(), contracts.end(), [&](const auto &entry) {
              return entry.boundary == boundary && entry.state.field == state->field &&
                     entry.state.index == state->index && entry.state.expected == state->expected &&
                     entry.state.actual == state->actual && entry.state.bytes == state->bytes &&
                     state->bytes != 0;
            });
        if (contract == contracts.end())
          return state;
        observed[contract - contracts.begin()] = true;
        const auto range =
            std::find_if(left.ranges.begin(), left.ranges.end(),
                         [&](const auto &entry) { return entry.name == state->field; });
        const auto offset = range->offset + state->index * range->element_bytes;
        // Consume only this exact contract, then compare every remaining field and element.
        for (unsigned byte = 0; byte < state->bytes; ++byte)
          left.bytes[offset + byte] = static_cast<std::uint8_t>(state->actual >> (byte * 8));
      }
      return {};
    };
    if (auto state =
            compare_state(guest_checkpoint(expected.console), guest_checkpoint(actual.console)))
      return state;
    if (auto state = compare_state(output_checkpoint(expected), output_checkpoint(actual)))
      return state;
    for (std::size_t n = 0; n < contracts.size(); ++n)
      if (contracts[n].boundary == boundary && !observed[n])
        return StateDifference{contracts[n].state.field + ".contract", contracts[n].state.index,
                               contracts[n].state.actual, contracts[n].state.expected,
                               contracts[n].state.bytes};
    return {};
  };
  if (auto difference = checkpoint(0))
    return Difference{0, *difference};
  for (std::size_t n = 0; n < input.actions.size(); ++n) {
    const auto &action = input.actions[n];
    state::Archive::require(valid(action, false));
    // Native RSP batches can exceed the requested budget. Scalar replay runs to that same
    // clock boundary; an incorrect issue cost still differs in clocks, PC or pipeline hazards.
    if (action.kind == ActionKind::CpuSteps) {
      auto step = action;
      step.value = 1;
      for (std::uint64_t count = 0; count < action.value; ++count) {
        actual.apply(step, Mode::Native);
        expected.apply(step, Mode::Interpreted, actual.console.signal().clocks());
      }
    } else {
      actual.apply(action, Mode::Native);
      expected.apply(action, Mode::Interpreted, actual.console.signal().clocks());
    }
    if (observe) {
      observe(expected.console, Mode::Interpreted, n + 1);
      observe(actual.console, Mode::Native, n + 1);
    }
    if (auto difference = checkpoint(n + 1))
      return Difference{n + 1, *difference};
  }
  return {};
}

Input reduce(Input input, const std::function<bool(const Input &)> &fails, unsigned attempts) {
  enum class Part { Actions, Initial, Contracts };
  bool changed;
  do {
    changed = false;
    for (const auto group : {Part::Actions, Part::Initial, Part::Contracts}) {
      std::size_t n = 0;
      const auto size = [&] {
        return group == Part::Actions   ? input.actions.size()
               : group == Part::Initial ? input.initial.size()
                                        : input.contracts.size();
      };
      while (attempts && n < size()) {
        auto candidate = input;
        if (group == Part::Contracts)
          candidate.contracts.erase(candidate.contracts.begin() + n);
        else {
          auto &actions = group == Part::Initial ? candidate.initial : candidate.actions;
          actions.erase(actions.begin() + n);
          if (group == Part::Actions) {
            std::erase_if(candidate.contracts,
                          [&](const auto &entry) { return entry.boundary == n + 1; });
            for (auto &contract : candidate.contracts)
              if (contract.boundary > n + 1)
                --contract.boundary;
          }
        }
        --attempts;
        if (fails(candidate)) {
          input = std::move(candidate);
          changed = true;
        } else
          ++n;
      }
    }
  } while (changed && attempts);
  return input;
}

void print(const Difference &difference) {
  const auto &state = difference.state;
  std::cerr << "Replay boundary " << difference.boundary << ' ' << state.field << '[' << state.index
            << "] expected 0x" << std::hex << state.expected << ", got 0x" << state.actual
            << std::dec << '\n';
}

} // namespace test::machine_replay
