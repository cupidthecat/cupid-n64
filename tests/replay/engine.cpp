#include "engine.hpp"
#include "core/cpu/fpu/environment.hpp"
#include <algorithm>
#include <bit>
#include <iostream>

namespace test::machine_replay {
using namespace cupid::n64;
namespace {

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
  std::vector<std::uint64_t> output;

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
          output.insert(output.end(),
                        {0, console.cpu().state().clocks, std::bit_cast<std::uint64_t>(sample.left),
                         std::bit_cast<std::uint64_t>(sample.right)});
        },
        [this](unsigned rate) { output.insert(output.end(), {1, rate}); });
    console.video().connect_frame([this](bool field) {
      output.insert(output.end(), {2, console.cpu().state().clocks, unsigned(field)});
    });
    console.display().connect(
        [this](std::span<const std::uint32_t> words) {
          output.insert(output.end(), {3, console.cpu().state().clocks, words.size()});
          output.insert(output.end(), words.begin(), words.end());
        },
        [this] { output.push_back(4); });
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

std::optional<StateDifference> outputs(const Machine &expected, const Machine &actual) {
  const auto common = std::min(expected.output.size(), actual.output.size());
  for (std::size_t n = 0; n < common; ++n)
    if (expected.output[n] != actual.output[n])
      return StateDifference{"output.device", n, expected.output[n], actual.output[n], 8};
  if (expected.output.size() != actual.output.size())
    return StateDifference{"output.device.size", common, expected.output.size(),
                           actual.output.size(), 0};
  return {};
}

} // namespace

std::optional<Difference> compare(const Input &input, const Observer &observe) {
  Machine expected(input), actual(input);
  for (const auto &action : input.initial) {
    state::Archive::require(valid(action, true));
    expected.apply(action, Mode::Interpreted);
  }
  const auto start = CoreState::capture(expected.console);
  CoreState::restore(actual.console, start);
  expected.output.clear();
  actual.output.clear();
  const auto checkpoint = [&]() -> std::optional<StateDifference> {
    if (auto state =
            guest_checkpoint(expected.console).difference(guest_checkpoint(actual.console)))
      return state;
    return outputs(expected, actual);
  };
  if (auto difference = checkpoint())
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
    if (auto difference = checkpoint())
      return Difference{n + 1, *difference};
  }
  return {};
}

Input reduce(Input input, const std::function<bool(const Input &)> &fails, unsigned attempts) {
  bool changed;
  do {
    changed = false;
    for (bool initial : {false, true}) {
      std::size_t n = 0;
      while (attempts && n < (initial ? input.initial : input.actions).size()) {
        auto candidate = input;
        auto &actions = initial ? candidate.initial : candidate.actions;
        actions.erase(actions.begin() + n);
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
