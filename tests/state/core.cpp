#include "../support/test.hpp"
#include "checkpoint.hpp"
#include "core/controller/accessories/cartridge/handheld.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

using namespace cupid::n64;

struct Machine {
  Console console;
  std::vector<std::uint64_t> audio;
  unsigned rate_calls = 0;

  static ConsoleConfig config(bool expansion, VideoRegion region) {
    ConsoleConfig config;
    config.expansion = expansion;
    config.region = region;
    config.eeprom_size = 512;
    config.flash_model = FlashModel::Mx29l1101A;
    config.rtc_present = true;
    config.rtc_clock = [] { return std::int64_t(946684800); };
    config.disk_clock = config.rtc_clock;
    config.random_seed = 0;
    return config;
  }

  Machine(bool expansion = true, VideoRegion region = VideoRegion::Ntsc)
      : console(config(expansion, region)) {
    std::array<std::uint8_t, 0x7c0> firmware{};
    std::array<std::uint8_t, 4096> rom{};
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    test::equal(console.load(rom, firmware), true);
    console.write(0x04700008, 4, 0);
    console.write(0x0470000c, 4, 0x14);
    console.write(0x04300000, 4, 0x10f);
    console.write(0x03f80008, 4, 0x00080008);
    const auto chips = console.ram().size() / 0x200000;
    for (unsigned chip = 0; chip < chips; ++chip) {
      console.ram().write_word(0x03f0000c, 0x02000000);
      console.ram().write_word(0x03f00004, (chips + chip) * 2 << 26);
    }
    for (unsigned chip = 0; chip < chips; ++chip)
      console.ram().write_word(0x03f00004 + (chips + chip) * 2 * 0x400, chip * 2 << 26);
    test::equal(console.ram().identity(), true);
    console.cpu().write_control(Status, 0x30000000);
    console.cpu().write_control(Compare, 0x10000000);
    console.cpu().set_pc(0xffffffff80001000ull);
    console.cpu().state().gpr[2] = 0xffffffff80002000ull;
    console.write(0x1000, 4, test::i(9, 1, 1, 1));
    console.write(0x1004, 4, test::i(0x2b, 2, 1, 0));
    console.write(0x1008, 4, test::i(4, 0, 0, 0xfffd));
    console.write(0x100c, 4, test::i(9, 3, 3, 7));
    test::equal(console.read(0x1000, 4).value, test::i(9, 1, 1, 1));
    console.connect_controller(0, true);
    console.connect_mouse(1);
    console.connect_gamecube_controller(2);
    console.controller(0).memory_pak(3);
    console.controller(0).input(0x8030, 17, -29);
    console.mouse(1).input(true, false, 18, -11);
    console.audio().connect(
        [this](StereoSample sample) {
          audio.push_back(std::bit_cast<std::uint64_t>(sample.left));
          audio.push_back(std::bit_cast<std::uint64_t>(sample.right));
        },
        [this](unsigned) { ++rate_calls; });
  }

  void run(unsigned mode, unsigned count) {
    for (unsigned n = 0; n < count; ++n) {
      if (mode == 0)
        console.step();
      else if (mode == 1) {
        const auto target = console.cpu().state().clocks + 33;
        console.cpu().run_interpreted_block(target);
        console.synchronize();
      } else
        console.run_interval(33);
    }
  }
};

bool rejects(Console &console, std::span<const std::uint8_t> data) {
  try {
    CoreState::restore(console, data);
  } catch (const state::InvalidState &) {
    return true;
  }
  return false;
}

void rewrite_checksum(std::vector<std::uint8_t> &bytes) {
  const auto value = state::Archive::fingerprint(std::span(bytes).first(bytes.size() - 8));
  for (unsigned n = 0; n < 8; ++n)
    bytes[bytes.size() - 8 + n] = static_cast<std::uint8_t>(value >> (n * 8));
}

void checkpoint_tests() {
  Machine machine;
  auto &console = machine.console;
  machine.run(2, 3);
  const auto before = CoreState::capture(console);
  const auto checkpoint = CoreState::checkpoint(console);
  test::equal(checkpoint.bytes == before, true);
  test::equal(CoreState::capture(console) == before, true);
  test::equal(checkpoint.difference(CoreState::checkpoint(console)).has_value(), false);
  test::checkpoint_layout(checkpoint);
  test::checkpoint_faults(checkpoint, [](std::string_view) { return true; });
  for (const auto field :
       {"pif.devices", "disk.bus_view", "cpu.tlb_lookup[0].entry", "rsp.dma.pending.local_address",
        "pi.bus_address", "ram.words", "events.heap[511].valid", "cpu.execution_history"}) {
    const auto found = std::find_if(checkpoint.ranges.begin(), checkpoint.ranges.end(),
                                    [&](const auto &range) { return range.name == field; });
    test::equal(found != checkpoint.ranges.end(), true);
    if (found != checkpoint.ranges.end())
      test::equal(found->bytes > 0, true);
  }
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto invalid = checkpoint;
    if (fault == 0)
      invalid.ranges.front().offset = 1;
    if (fault == 1)
      invalid.ranges.front().element_bytes = 0;
    if (fault == 2)
      invalid.bytes.pop_back();
    if (fault == 3)
      invalid.ranges.pop_back();
    if (fault == 4)
      invalid.ranges.front().element_bytes = 8;
    const auto difference = checkpoint.difference(invalid);
    test::equal(difference.has_value(), true);
    if (difference)
      test::equal(difference->field == "layout.invalid", true);
  }
  console.cpu().state().gpr[17] ^= 0xa5f0;
  const auto gpr = checkpoint.difference(CoreState::checkpoint(console));
  test::equal(gpr.has_value(), true);
  if (gpr) {
    test::equal(gpr->field == "cpu.gpr", true);
    test::equal(gpr->index, 17);
    test::equal(gpr->expected ^ gpr->actual, 0xa5f0);
  }
  CoreState::restore(console, before);
  console.cpu().write_control(Compare, 0x12345678);
  const auto cp0 = checkpoint.difference(CoreState::checkpoint(console));
  test::equal(cp0.has_value(), true);
  if (cp0) {
    test::equal(cp0->field == "cpu.cp0", true);
    test::equal(cp0->index, Compare);
    test::equal(cp0->actual, 0x12345678);
  }
  CoreState::restore(console, before);
  console.write(0x2800, 4, 0x12345678);
  const auto ram = checkpoint.difference(CoreState::checkpoint(console));
  test::equal(ram.has_value(), true);
  if (ram) {
    test::equal(ram->field == "ram.words", true);
    test::equal(ram->index, 0x2800 / 4);
    test::equal(ram->actual, 0x12345678);
  }
  CoreState::restore(console, before);
  console.signal().state().vectors[9].lanes[6] = 0x9abc;
  const auto vector = checkpoint.difference(CoreState::checkpoint(console));
  test::equal(vector.has_value(), true);
  if (vector) {
    test::equal(vector->field == "rsp.vector", true);
    test::equal(vector->index, 9 * 8 + 6);
    test::equal(vector->actual, 0x9abc);
  }
  const auto perturb = [&](std::string_view field, std::size_t index, auto change) {
    CoreState::restore(console, before);
    change();
    const auto difference = checkpoint.difference(CoreState::checkpoint(console));
    if (difference && difference->field != field)
      std::cerr << "Checkpoint perturbation " << field << " first changed " << difference->field
                << '[' << difference->index << "]\n";
    test::equal(difference.has_value(), true);
    if (difference) {
      test::equal(difference->field == field, true);
      test::equal(difference->index, index);
    }
  };
  perturb("ri.registers", 0, [&] { console.write(0x04700000, 4, 3); });
  perturb("pif.ram", 13, [&] { console.pif().ram()[13] ^= 0x40; });
  perturb("pi.dram_address", 0, [&] { console.write(0x04600000, 4, 0x1230); });
  perturb("si.dram_address", 0, [&] { console.write(0x04800000, 4, 0x1430); });
  perturb("vi.registers", 2, [&] { console.video().write_word(8, 320); });
  perturb("audio.state.dac_rate", 0, [&] { console.audio().write_word(16, 0x1111); });
  perturb("controller[0].buttons", 0, [&] { console.controller(0).input(0x8031, 17, -29); });
  perturb("controller[0].ram.data", 0x9000,
          [&] { console.controller(0).pak_data()[0x9000] ^= 0x20; });
  perturb("mouse[1].x", 0, [&] { console.mouse(1).input(true, false, 19, -11); });
  perturb("eeprom.data", 31, [&] { console.eeprom().data()[31] ^= 0x80; });
  perturb("flash.data", 0x8000, [&] { console.flash().data()[0x8000] ^= 0x10; });
  perturb("ram.hidden.data", 19, [&] { console.ram().hidden()[19] ^= 1; });
}

void arcade_checkpoint_tests() {
  auto config = Machine::config(false, VideoRegion::Ntsc);
  config.arcade_profile = ArcadeProfile::Standard;
  Console console(config);
  auto *arcade = console.arcade();
  test::equal(arcade != nullptr, true);
  if (!arcade)
    return;
  const auto checkpoint = CoreState::checkpoint(console);
  test::checkpoint_layout(checkpoint);
  test::equal(checkpoint.bytes == CoreState::capture(console), true);
  arcade->input().players[1].buttons[8] = true;
  const auto difference = checkpoint.difference(CoreState::checkpoint(console));
  test::equal(difference.has_value(), true);
  if (difference) {
    test::equal(difference->field == "arcade.players[1].buttons", true);
    test::equal(difference->index, 8);
    test::equal(difference->actual, 1);
  }
}

void compare_checkpoint(const StateCheckpoint &expected, Console &console, unsigned mode,
                        unsigned boundary) {
  const auto difference = expected.difference(CoreState::checkpoint(console));
  if (difference)
    std::cerr << "Replay mode " << mode << " boundary " << boundary << ' ' << difference->field
              << '[' << difference->index << "] expected 0x" << std::hex << difference->expected
              << ", got 0x" << difference->actual << std::dec << '\n';
  test::equal(difference.has_value(), false);
}

void continuation_tests() {
  for (const auto region : {VideoRegion::Ntsc, VideoRegion::Pal})
    for (bool expansion : {false, true})
      for (unsigned mode = 0; mode < 3; ++mode) {
        Machine machine(expansion, region);
        auto &c = machine.console;
        machine.run(mode, 9);
        c.cpu().execute(test::i(0x30, 2, 4, 0));
        c.signal().state().vectors[3].lanes = {1, 2, 3, 4, 5, 6, 7, 8};
        c.signal().execute(0x4a031904);
        c.signal().write_io(0, 0x1000);
        c.signal().write_io(4, 0x2000);
        c.signal().write_io(8, 0x0010107f);
        c.write(0x04600000, 4, 0x3000);
        c.write(0x04600004, 4, 0x10000000);
        c.write(0x0460000c, 4, 127);
        c.write(0x04800000, 4, 0x4000);
        c.write(0x04800010, 4, 0x1fc007c0);
        c.pif().ram()[63] = 0;
        std::array<std::uint8_t, 10> command{5, 3, 1, 2, 3, 4, 5, 6, 7, 8};
        std::array<std::uint8_t, 1> response{};
        test::equal(c.eeprom().communicate(command, response).valid, true);
        for (unsigned n = 0; n < 64; ++n)
          c.write(0x2800 + n * 4, 4, 0x20008000 + n);
        c.audio().write_word(16, 0);
        c.audio().advance(static_cast<std::uint32_t>(c.audio().clocks()));
        c.audio().write_word(0, 0x2800);
        c.audio().write_word(4, 0x100);
        c.audio().write_word(8, 1);
        c.video().scanout(c.ram());
        const auto initial = CoreState::capture(c);
        const auto initial_rate_calls = machine.rate_calls;
        machine.audio.clear();
        machine.run(mode, 80);
        c.cpu().execute(test::i(0x38, 2, 4, 4));
        c.cpu().execute(test::i(0x2f, 2, 0x15, 0));
        const auto expected = CoreState::checkpoint(c);
        const auto expected_audio = machine.audio;
        c.write(0x1000, 4, test::i(9, 1, 1, 0x77));
        c.cpu().state().gpr[5] = 0xffffffff80001000ull;
        c.cpu().execute(test::i(0x2f, 5, 0x10, 0));
        c.cpu().set_pc(0xffffffff80001000ull);
        machine.run(mode, 10);
        c.cpu().request_nmi();
        c.write(0x2000, 4, 0xdeadbeef);
        c.controller(0).pak_data()[0x9000] = 0x57;
        c.flash().data()[0x8000] = 0x31;
        c.eeprom().data()[31] = 0x83;
        CoreState::restore(c, initial);
        test::equal(CoreState::capture(c) == initial, true);
        test::equal(machine.rate_calls, initial_rate_calls);
        machine.audio.clear();
        machine.run(mode, 80);
        c.cpu().execute(test::i(0x38, 2, 4, 4));
        c.cpu().execute(test::i(0x2f, 2, 0x15, 0));
        compare_checkpoint(expected, c, mode, 80);
        test::equal(machine.audio == expected_audio, true);
        test::equal(!machine.audio.empty(), true);
        Machine fresh(expansion, region);
        CoreState::restore(fresh.console, initial);
        test::equal(fresh.rate_calls, 2);
        test::equal(CoreState::capture(fresh.console) == initial, true);
        fresh.audio.clear();
        fresh.run(mode, 80);
        fresh.console.cpu().execute(test::i(0x38, 2, 4, 4));
        fresh.console.cpu().execute(test::i(0x2f, 2, 0x15, 0));
        compare_checkpoint(expected, fresh.console, mode, 80);
        test::equal(fresh.audio == expected_audio, true);
      }
}

void relocation_tests() {
  Machine layout(false);
  const auto cpu_offset = reinterpret_cast<std::uintptr_t>(&layout.console.cpu()) -
                          reinterpret_cast<std::uintptr_t>(&layout);
  std::vector<std::uint8_t> first_storage(sizeof(Machine) + 131072);
  std::vector<std::uint8_t> second_storage(first_storage.size());
  const auto place = [&](auto &storage, unsigned low) {
    const auto begin = reinterpret_cast<std::uintptr_t>(storage.data());
    auto address = ((begin + cpu_offset + 65535) & ~std::uintptr_t(65535)) + low - cpu_offset;
    test::equal(address % alignof(Machine), 0);
    test::equal(address >= begin && address + sizeof(Machine) <= begin + storage.size(), true);
    return std::construct_at(reinterpret_cast<Machine *>(address), false);
  };
  const auto release = [](Machine *machine) { std::destroy_at(machine); };
  std::unique_ptr<Machine, decltype(release)> first(place(first_storage, 0), release);
  std::unique_ptr<Machine, decltype(release)> second(place(second_storage, alignof(Machine)),
                                                     release);
  test::equal(reinterpret_cast<std::uintptr_t>(&first->console.cpu()) & 65535, 0);
  test::equal(reinterpret_cast<std::uintptr_t>(&second->console.cpu()) & 65535, alignof(Machine));
  first->run(2, 9);
  second->run(2, 9);
  compare_checkpoint(CoreState::checkpoint(first->console), second->console, 2, 9);
  const auto saved = CoreState::capture(first->console);
  first->run(2, 80);
  const auto expected = CoreState::checkpoint(first->console);
  CoreState::restore(second->console, saved);
  second->run(2, 80);
  compare_checkpoint(expected, second->console, 2, 80);
}

void invalid_tests() {
  Machine machine;
  machine.run(2, 7);
  const auto valid = CoreState::capture(machine.console);
  for (unsigned fault = 0; fault < 7; ++fault) {
    auto bytes = valid;
    if (fault == 0)
      bytes[0] ^= 1;
    if (fault == 1) {
      bytes[8] = 5;
      rewrite_checksum(bytes);
    }
    if (fault == 2) {
      bytes[16] = 2;
      rewrite_checksum(bytes);
    }
    if (fault == 3) {
      bytes[45] ^= 1;
      rewrite_checksum(bytes);
    }
    if (fault == 4) {
      bytes.insert(bytes.end() - 8, 0x42);
      rewrite_checksum(bytes);
    }
    if (fault == 5) {
      bytes.erase(bytes.end() - 20, bytes.end() - 8);
      rewrite_checksum(bytes);
    }
    if (fault == 6)
      bytes.resize(bytes.size() / 2);
    test::equal(rejects(machine.console, bytes), true);
    test::equal(CoreState::capture(machine.console) == valid, true);
  }
  Machine wrong_ram(false);
  const auto before = CoreState::capture(wrong_ram.console);
  test::equal(rejects(wrong_ram.console, valid), true);
  test::equal(CoreState::capture(wrong_ram.console) == before, true);
}

void dispatch_history_tests() {
  for (unsigned length : {0u, 4u, 8u, 16u, 31u})
    for (unsigned budget : {1u, 3u, 9u, 33u}) {
      Machine machine;
      auto &c = machine.console;
      c.write(0x1800, 4, test::i(4, 0, 0, static_cast<std::uint16_t>(length + 1)));
      c.write(0x1804, 4, 0);
      for (unsigned n = 0; n < length; ++n)
        c.write(0x1808 + n * 4, 4, test::i(9, 6, 6, 1));
      c.write(0x1808 + length * 4, 4, test::r(8, 31, 0, 0));
      c.write(0x180c + length * 4, 4, test::i(9, 7, 7, 1));
      c.cpu().state().gpr[31] = 0xffffffff80001800ull;
      c.cpu().set_pc(0xffffffff80001800ull);
      c.run_interval(1);
      test::equal(c.cpu().state().pc, 0xffffffff80001808ull + length * 4);
      c.cpu().state().gpr[5] = 0xffffffff80001800ull;
      c.cpu().execute(test::i(0x2f, 5, 0x10, 0));
      c.cpu().set_pc(0xffffffff80001808ull);
      const auto initial = CoreState::capture(c);
      if (length == 4 && budget == 1) {
        const auto put = [](std::vector<std::uint8_t> &bytes, unsigned size, std::uint64_t value) {
          for (unsigned n = 0; n < size; ++n)
            bytes.push_back(static_cast<std::uint8_t>(value >> (n * 8)));
        };
        std::vector<std::uint8_t> recipe;
        put(recipe, 8, 0xffffffff80001800ull);
        put(recipe, 4, 0x20000002 | (15u << 15));
        put(recipe, 4, 0x1800);
        const auto start =
            std::search(initial.begin(), initial.end(), recipe.begin(), recipe.end());
        test::equal(start != initial.end(), true);
        if (start != initial.end()) {
          const auto offset = static_cast<std::size_t>(start - initial.begin());
          for (unsigned fault = 0; fault < 4; ++fault) {
            auto broken = initial;
            if (fault == 0)
              broken[offset + 12] ^= 1;
            if (fault == 1)
              broken[offset + 11] |= 0x80;
            if (fault == 2)
              std::fill_n(broken.begin() + offset + 16, 8, std::uint8_t{0xff});
            if (fault == 3)
              broken[offset + 28 + (length + 4) * 4] = 1;
            rewrite_checksum(broken);
            test::equal(rejects(c, broken), true);
            test::equal(CoreState::capture(c) == initial, true);
          }
          auto relocated = initial;
          constexpr std::uint64_t saved_cost = 4 * 1024 * 1024;
          for (unsigned byte = 0; byte < 8; ++byte)
            relocated[offset + 16 + byte] = static_cast<std::uint8_t>(saved_cost >> (byte * 8));
          rewrite_checksum(relocated);
          Machine restored;
          CoreState::restore(restored.console, relocated);
          test::equal(CoreState::capture(restored.console) == relocated, true);
          restored.console.run_interval(budget);
          const auto regenerated = CoreState::capture(restored.console);
          const auto block =
              std::search(regenerated.begin(), regenerated.end(), recipe.begin(), recipe.end());
          test::equal(block != regenerated.end(), true);
          if (block != regenerated.end())
            for (unsigned byte = 0; byte < 8; ++byte)
              test::equal(block[16 + byte], static_cast<std::uint8_t>(saved_cost >> (byte * 8)));
        }
      }
      c.run_interval(budget);
      const auto expected = CoreState::capture(c);
      CoreState::restore(c, initial);
      c.run_interval(budget);
      test::equal(CoreState::capture(c) == expected, true);
      Machine fresh;
      CoreState::restore(fresh.console, initial);
      fresh.console.run_interval(budget);
      test::equal(CoreState::capture(fresh.console) == expected, true);
    }
}

void cache_page_tests() {
  Machine machine;
  auto &c = machine.console;
  c.run_interval(1);
  for (unsigned page = 0; page < 4096; ++page) {
    const auto offset = 0x00800040ull + page * 4096ull;
    const auto pc = 0xffffffff80000000ull + offset + (offset >= 0x01000000 ? 0x01000000 : 0);
    c.cpu().set_pc(pc);
    const auto clocks = c.cpu().state().clocks;
    c.run_interval(1);
    test::equal(c.frozen(), false);
    test::equal(c.cpu().state().pc, pc + 4);
    test::equal(c.cpu().state().clocks > clocks, true);
    if (page == 4094) {
      const auto boundary = CoreState::capture(c);
      CoreState::restore(c, boundary);
      test::equal(CoreState::capture(c) == boundary, true);
    }
  }
  std::vector<std::uint8_t> initial;
  try {
    initial = CoreState::capture(c);
  } catch (const state::InvalidState &) {
    test::equal(false, true);
    return;
  }
  const std::array<std::uint8_t, 9> cache_start{1, 0x10, 0, 0, 0, 0x10, 0, 0, 1};
  const auto count =
      std::search(initial.begin(), initial.end(), cache_start.begin(), cache_start.end());
  test::equal(count != initial.end(), true);
  if (count != initial.end()) {
    auto broken = initial;
    const auto offset = static_cast<std::size_t>(count - initial.begin());
    broken[offset + 2] = 0x10;
    rewrite_checksum(broken);
    test::equal(rejects(c, broken), true);
    test::equal(CoreState::capture(c) == initial, true);
  }
  CoreState::restore(c, initial);
  test::equal(CoreState::capture(c) == initial, true);
  Machine fresh;
  CoreState::restore(fresh.console, initial);
  test::equal(CoreState::capture(fresh.console) == initial, true);
  c.cpu().set_pc(0xffffffff80001000ull);
  c.run_interval(33);
  const auto expected = CoreState::capture(c);
  fresh.console.cpu().set_pc(0xffffffff80001000ull);
  fresh.console.run_interval(33);
  test::equal(CoreState::capture(fresh.console) == expected, true);
  CoreState::restore(c, initial);
  c.cpu().set_pc(0xffffffff80001000ull);
  c.run_interval(33);
  test::equal(CoreState::capture(c) == expected, true);
}

void rsp_cache_history_tests() {
  Machine machine;
  auto &rsp = machine.console.signal();
  for (unsigned block = 0; block < 65537; ++block) {
    const auto pc = (block & 1023) * 4;
    const auto opcode = 13 | ((block >> 10) << 6);
    rsp.write_local(0x1000 | pc, 4, opcode);
    rsp.write_local(0x1000 | ((pc + 4) & 0xfff), 4, 0);
    rsp.write_status(0, pc);
    rsp.write_io(16, 5);
    rsp.advance(static_cast<std::uint32_t>(rsp.clocks() + 1));
    test::equal(rsp.status().halted, true);
    test::equal(rsp.status().broken, true);
    test::equal(rsp.read_local(0x1000 | pc, 4), opcode);
  }
  std::vector<std::uint8_t> initial;
  try {
    initial = CoreState::capture(machine.console);
  } catch (const state::InvalidState &) {
    test::equal(false, true);
    return;
  }
  const std::array<std::uint8_t, 33> cache_start{1, 0, 1, 0};
  const auto count =
      std::search(initial.begin(), initial.end(), cache_start.begin(), cache_start.end());
  test::equal(count != initial.end(), true);
  if (count != initial.end()) {
    auto broken = initial;
    const auto offset = static_cast<std::size_t>(count - initial.begin());
    broken[offset + 2] = 0x40;
    broken[offset + 3] = 2;
    rewrite_checksum(broken);
    test::equal(rejects(machine.console, broken), true);
    test::equal(CoreState::capture(machine.console) == initial, true);
  }
  CoreState::restore(machine.console, initial);
  test::equal(CoreState::capture(machine.console) == initial, true);
  Machine fresh;
  CoreState::restore(fresh.console, initial);
  test::equal(CoreState::capture(fresh.console) == initial, true);
  const auto resume = [](Machine &m) {
    auto &signal = m.console.signal();
    signal.write_local(0x1000, 4, test::i(9, 1, 1, 1));
    signal.write_local(0x1004, 4, 13);
    signal.write_status(0, 0);
    signal.write_io(16, 5);
    signal.advance(static_cast<std::uint32_t>(signal.clocks() + 1));
    test::equal(signal.state().gpr[1], 1);
    test::equal(signal.status().halted, true);
  };
  resume(machine);
  const auto expected = CoreState::capture(machine.console);
  resume(fresh);
  test::equal(CoreState::capture(fresh.console) == expected, true);
  CoreState::restore(machine.console, initial);
  resume(machine);
  test::equal(CoreState::capture(machine.console) == expected, true);
}

void memory_binding_tests() {
  for (unsigned memory = 0; memory < 2; ++memory) {
    Machine machine;
    machine.run(2, 3);
    const auto original = CoreState::capture(machine.console);
    if (memory == 0)
      static_cast<void>(machine.console.ram().words());
    else
      static_cast<void>(machine.console.signal().imem());
    const auto exposed = CoreState::capture(machine.console);
    test::equal(original != exposed, true);
    test::equal(rejects(machine.console, original), true);
    test::equal(CoreState::capture(machine.console) == exposed, true);
    CoreState::restore(machine.console, exposed);
    test::equal(CoreState::capture(machine.console) == exposed, true);
    Machine fresh;
    CoreState::restore(fresh.console, exposed);
    test::equal(CoreState::capture(fresh.console) == exposed, true);
  }
}

void accessory_tests() {
  Machine machine;
  auto cartridge = std::make_shared<HandheldCartridge>([] { return std::int64_t(946684800); });
  std::vector<std::uint8_t> rom(0x10000);
  HandheldCartridge::Config config;
  config.board = HandheldCartridge::Board::Mbc7;
  config.eeprom_size = 256;
  test::equal(cartridge->load(rom, config), true);
  machine.console.controller(1).transfer_pak(cartridge);
  machine.console.controller(2).bio_sensor([] { return std::uint64_t(1234567); });
  cartridge->motion(-103, 710);
  cartridge->write(0, 10);
  cartridge->write(0x4000, 0x40);
  cartridge->write(0xa080, 0x80);
  cartridge->write(0xa080, 0xc2);
  machine.console.controller(2).sensor().update();
  const auto initial = CoreState::capture(machine.console);
  const auto checkpoint = CoreState::checkpoint(machine.console);
  test::checkpoint_layout(checkpoint);
  test::equal(checkpoint.bytes == initial, true);
  cartridge->motion(100, -900);
  cartridge->write(0xa080, 0x80);
  cartridge->write(0xa080, 0xc0);
  cartridge->eeprom()[3] = 0x49;
  machine.console.controller(2).sensor().beats_per_minute(130);
  CoreState::restore(machine.console, initial);
  test::equal(CoreState::capture(machine.console) == initial, true);
  test::equal(checkpoint.difference(CoreState::checkpoint(machine.console)).has_value(), false);
  machine.console.controller(1).disconnect_pak();
  const auto disconnected = CoreState::capture(machine.console);
  test::equal(rejects(machine.console, initial), true);
  test::equal(CoreState::capture(machine.console) == disconnected, true);
}

void conditional_checkpoint_tests() {
  Machine machine;
  auto &pad = machine.console.controller(0);
  pad.transfer_pak();
  const auto empty = CoreState::checkpoint(machine.console);
  auto cartridge = std::make_shared<HandheldCartridge>();
  std::vector<std::uint8_t> rom(0x8000);
  test::equal(cartridge->load(rom, {}), true);
  pad.transfer_pak(cartridge);
  const auto attached = CoreState::checkpoint(machine.console);
  test::equal(empty.ranges.size() < attached.ranges.size(), true);
  for (bool reverse : {false, true}) {
    const auto difference = reverse ? attached.difference(empty) : empty.difference(attached);
    test::equal(difference.has_value(), true);
    if (difference) {
      test::equal(difference->field == "controller[0].transfer.identity", true);
      test::equal(difference->expected, reverse ? 1 : 0);
      test::equal(difference->actual, reverse ? 0 : 1);
      test::equal(difference->bytes, 1);
    }
  }
}

void boot_identity_tests() {
  std::array<std::uint8_t, 0x7c0> firmware{};
  std::array<std::uint8_t, 4096> image{};
  image[0] = 0x80;
  image[1] = 0x37;
  image[2] = 0x12;
  image[3] = 0x40;
  const auto configuration = [](unsigned mode) {
    ConsoleConfig config;
    config.random_seed = 0;
    config.rtc_clock = [] { return std::int64_t(946684800); };
    config.disk_clock = config.rtc_clock;
    config.boot_verification = {bool(mode & 1), bool(mode & 2)};
    return config;
  };
  std::array<std::vector<std::uint8_t>, 4> states;
  for (unsigned mode = 0; mode < states.size(); ++mode) {
    Console console(configuration(mode));
    test::equal(console.load(image, firmware), true);
    states[mode] = CoreState::capture(console);
  }
  for (unsigned mode = 0; mode < states.size(); ++mode) {
    Console console(configuration(mode));
    test::equal(console.load(image, firmware), true);
    for (unsigned source = 0; source < states.size(); ++source) {
      const auto before = CoreState::capture(console);
      bool accepted = true;
      try {
        CoreState::restore(console, states[source]);
      } catch (const state::InvalidState &) {
        accepted = false;
      }
      test::equal(accepted, mode == source);
      test::equal(CoreState::capture(console) == (accepted ? states[source] : before), true);
    }
    console.pif().tick();
    console.pif().write_word(0x1fc007fc, 0x10);
    console.pif().write_word(0x1fc007fc, 0x20);
    console.pif().write_word(0x1fc007fc, 0x40);
    test::equal(static_cast<unsigned>(console.pif().state()),
                static_cast<unsigned>((mode & 1) ? Pif::State::Error : Pif::State::Terminate));
  }
}

void cpu_cache_context_tests() {
  Machine machine(false);
  auto &console = machine.console;
  auto &cpu = console.cpu();
  console.write(0x1000, 4, test::i(35, 28, 6, 0));
  console.write(0x1004, 4, test::i(35, 29, 7, 0));
  console.write(0x1008, 4, test::r(8, 31, 0, 0));
  console.write(0x100c, 4, 0);
  for (const auto gp : {0xffffffff80010000ull, 0xffffffff80010001ull, 0xffffffffa0010000ull}) {
    for (const auto sp : {0xffffffff80010000ull, 0xffffffff80010004ull, 0xffffffff80010001ull}) {
      cpu.write_control(Status, 0x30000000);
      cpu.state().gpr[28] = gp;
      cpu.state().gpr[29] = sp;
      cpu.state().gpr[31] = 0xffffffff80003000ull;
      cpu.set_pc(0xffffffff80001000ull);
      test::equal(cpu.run_block(cpu.state().clocks + 1000), true);
      const auto initial = CoreState::capture(console);
      cpu.write_control(Status, 0x30000000);
      cpu.set_pc(0xffffffff80001000ull);
      test::equal(cpu.run_block(cpu.state().clocks + 1000), true);
      const auto future = CoreState::capture(console);
      CoreState::restore(console, initial);
      test::equal(CoreState::capture(console) == initial, true);
      cpu.write_control(Status, 0x30000000);
      cpu.set_pc(0xffffffff80001000ull);
      test::equal(cpu.run_block(cpu.state().clocks + 1000), true);
      test::equal(CoreState::capture(console) == future, true);
    }
  }
  for (bool valid : {false, true}) {
    cpu.write_control(Status, 0x30000000);
    for (unsigned cache : {0u, 1u}) {
      cpu.state().gpr[4] = 0xffffffff80000000ull | (cache ? 0x10u : 0x20u);
      cpu.write_control(TagLo, 0x3500 | (valid ? 0x80 : 0));
      cpu.execute(test::i(47, 4, 8 + cache, 0));
    }
    const auto saved = CoreState::checkpoint(console);
    for (const auto name :
         {"cpu.icache[1].tag", "cpu.dcache[1].tag", "cpu.icache[1].valid", "cpu.dcache[1].valid"}) {
      const auto range = std::find_if(saved.ranges.begin(), saved.ranges.end(),
                                      [&](const auto &r) { return r.name == name; });
      test::equal(range != saved.ranges.end(), true);
      if (range == saved.ranges.end())
        continue;
      const bool tag = std::string_view(name).ends_with("tag");
      test::equal(range->bytes, tag ? 4 : 1);
      std::uint32_t bits = 0;
      for (unsigned byte = 0; byte < range->bytes; ++byte)
        bits |= std::uint32_t(saved.bytes[range->offset + byte]) << (byte * 8);
      test::equal(bits, tag ? 0x35000u : unsigned(valid));
    }
    for (unsigned cache : {0u, 1u}) {
      cpu.state().gpr[4] = 0xffffffff80000000ull | (cache ? 0x10u : 0x20u);
      cpu.write_control(TagLo, 0);
      cpu.execute(test::i(47, 4, 8 + cache, 0));
    }
    CoreState::restore(console, saved.bytes);
    test::equal(CoreState::capture(console) == saved.bytes, true);
    for (unsigned cache : {0u, 1u}) {
      cpu.state().gpr[4] = 0xffffffff80000000ull | (cache ? 0x10u : 0x20u);
      cpu.execute(test::i(47, 4, 4 + cache, 0));
      test::equal(cpu.read_control(TagLo), 0x3500u | (valid ? (cache ? 0xc0u : 0x80u) : 0));
    }
  }
}

void cpu_extension_pointer_tests() {
  for (bool native : {false, true}) {
    for (unsigned base : {28u, 29u}) {
      for (unsigned function : {0x20u, 0x29u}) {
        Machine machine(false);
        auto &console = machine.console;
        auto &cpu = console.cpu();
        console.diagnostics().set_homebrew(true);
        cpu.state().gpr[base] = 0xffffffff80010000ull;
        cpu.state().gpr[31] = 0xffffffff80003000ull;
        cpu.state().gpr[4] = 0xffffffff80000000ull;
        cpu.write_control(TagLo, 0x08000080);
        cpu.execute(test::i(47, 4, 9, 0));
        console.write(0x1000, 4,
                      0x42000000u | (base << (function == 0x20 ? 20 : 15)) | (2u << 6) | function);
        console.write(0x1004, 4, test::i(35, base, 2, 0));
        console.write(0x1008, 4, test::r(8, 31, 0, 0));
        console.write(0x100c, 4, 0);
        cpu.set_pc(0xffffffff80001000ull);
        test::equal(native ? cpu.run_block(1000) : cpu.run_interpreted_block(1000), true);
        test::equal(cpu.state().gpr[base], function == 0x20 ? 0x0e : 0);
        test::equal(cpu.read_control(Cause) & 0x7c, (function == 0x20 ? 4u : 2u) << 2);
        test::equal(cpu.read_control(Epc), 0xffffffff80001004ull);
      }
    }
  }
}

void inactive_video_tests() {
  for (const auto region : {VideoRegion::Ntsc, VideoRegion::Pal}) {
    ConsoleConfig config;
    config.region = region;
    config.random_seed = 0;
    config.homebrew = true;
    Console console(config);
    auto &video = console.video();
    video.write_word(16, 0);
    console.diagnostics().configure({.devices = diagnostic_bit(DiagnosticDevice::Ri)});
    unsigned fields = 0;
    video.connect_frame([&](bool) { ++fields; });
    const auto line = [&] { video.advance(static_cast<std::uint32_t>(video.clocks() + 1)); };
    const auto counter = [&] {
      const auto checkpoint = CoreState::checkpoint(console);
      const auto found =
          std::find_if(checkpoint.ranges.begin(), checkpoint.ranges.end(),
                       [](const auto &range) { return range.name == "vi.inactive_counter"; });
      test::equal(found != checkpoint.ranges.end(), true);
      if (found == checkpoint.ranges.end())
        return ~0u;
      test::equal(found->bytes, std::size_t(4));
      unsigned value = 0;
      for (unsigned n = 0; n < 4; ++n)
        value |= unsigned(checkpoint.bytes[found->offset + n]) << (n * 8);
      return value;
    };
    for (unsigned n = 0; n < 199; ++n) {
      line();
      test::equal(video.refreshed(), false);
    }
    test::equal(counter(), 199);
    const auto phase = CoreState::capture(console);
    console.cpu().state().clocks += static_cast<std::uint32_t>(video.clocks() + 1);
    console.synchronize();
    test::equal(video.refreshed(), true);
    test::equal(counter(), 0);
    test::equal(video.frames(), 0);
    test::equal(fields, 0);
    test::equal(console.interrupts().read_word(8) & 8, 0);
    const auto records = console.diagnostics().records();
    test::equal(std::none_of(records.begin(), records.end(),
                             [](const auto &record) {
                               return record.kind == DiagnosticKind::RefreshDisabled;
                             }),
                true);
    CoreState::restore(console, phase);
    test::equal(counter(), 199);
    test::equal(video.refreshed(), false);
    video.elapse(static_cast<std::uint32_t>(video.clocks() + 1));
    const auto clocks = console.cpu().state().clocks;
    test::equal(console.run_frame_interval(), 0);
    test::equal(console.cpu().state().clocks, clocks);
    test::equal(video.refreshed(), true);
    test::equal(counter(), 0);
    test::equal(video.frames(), 0);
    test::equal(fields, 0);
    test::equal(console.interrupts().read_word(8) & 8, 0);
    auto invalid = phase;
    invalid[invalid.size() - 12] = 200;
    rewrite_checksum(invalid);
    const auto unchanged = CoreState::capture(console);
    test::equal(rejects(console, invalid), true);
    test::equal(CoreState::capture(console) == unchanged, true);
    test::equal(video.refreshed(), true);
    auto legacy = phase;
    legacy[8] = 3;
    legacy.erase(legacy.end() - 12, legacy.end() - 8);
    rewrite_checksum(legacy);
    CoreState::restore(console, legacy);
    test::equal(counter(), 0);
    test::equal(video.refreshed(), false);
    for (unsigned n = 0; n < 199; ++n) {
      line();
      test::equal(video.refreshed(), false);
    }
    line();
    test::equal(video.refreshed(), true);
    test::equal(counter(), 0);
    test::equal(video.frames(), 0);
    test::equal(fields, 0);
    test::equal(console.interrupts().read_word(8) & 8, 0);
    video.power();
    test::equal(counter(), 0);
    test::equal(video.refreshed(), false);
    for (const auto warm : {false, true}) {
      CoreState::restore(console, phase);
      console.power(warm);
      test::equal(counter(), 199);
      test::equal(video.refreshed(), false);
      video.write_word(16, 0);
      line();
      test::equal(counter(), 0);
      test::equal(video.refreshed(), true);
      test::equal(video.frames(), 0);
      test::equal(fields, 0);
      test::equal(console.interrupts().read_word(8) & 8, 0);
    }
  }
}

} // namespace

void core_state_tests() {
  inactive_video_tests();
  cpu_extension_pointer_tests();
  cpu_cache_context_tests();
  boot_identity_tests();
  checkpoint_tests();
  arcade_checkpoint_tests();
  continuation_tests();
  relocation_tests();
  dispatch_history_tests();
  cache_page_tests();
  rsp_cache_history_tests();
  memory_binding_tests();
  invalid_tests();
  accessory_tests();
  conditional_checkpoint_tests();
}

int core_state_file_test(int argc, char **argv) {
  if (argc != 3)
    return 2;
  const std::filesystem::path directory(argv[2]);
  const auto write = [&](const char *name, std::span<const std::uint8_t> bytes) {
    std::ofstream file(directory / name, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    test::equal(bool(file), true);
  };
  const auto read = [&](const char *name) {
    std::ifstream file(directory / name, std::ios::binary | std::ios::ate);
    test::equal(bool(file), true);
    if (!file)
      throw std::runtime_error("Missing core state fixture");
    const auto size = file.tellg();
    if (size < 0 || size > 64 * 1024 * 1024)
      throw std::runtime_error("Invalid core state fixture size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    test::equal(bool(file), true);
    return bytes;
  };
  const auto audio_bytes = [](std::span<const std::uint64_t> samples) {
    std::vector<std::uint8_t> bytes;
    for (const auto sample : samples)
      for (unsigned byte = 0; byte < 8; ++byte)
        bytes.push_back(static_cast<std::uint8_t>(sample >> (byte * 8)));
    return bytes;
  };
  Machine machine;
  if (std::string_view(argv[1]) == "--write-state") {
    std::filesystem::create_directories(directory);
    machine.run(2, 11);
    machine.console.audio().write_word(16, 0);
    machine.console.audio().advance(static_cast<std::uint32_t>(machine.console.audio().clocks()));
    machine.console.audio().write_word(0, 0x2000);
    machine.console.audio().write_word(4, 0x100);
    machine.console.audio().write_word(8, 1);
    write("core.state", CoreState::capture(machine.console));
    machine.audio.clear();
    machine.run(2, 120);
    write("future.state", CoreState::capture(machine.console));
    write("future.audio", audio_bytes(machine.audio));
    test::equal(!machine.audio.empty(), true);
  } else if (std::string_view(argv[1]) == "--read-state") {
    const auto initial = read("core.state");
    CoreState::restore(machine.console, initial);
    test::equal(CoreState::capture(machine.console) == initial, true);
    machine.audio.clear();
    machine.run(2, 120);
    test::equal(CoreState::capture(machine.console) == read("future.state"), true);
    test::equal(audio_bytes(machine.audio) == read("future.audio"), true);
  } else {
    return 2;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
