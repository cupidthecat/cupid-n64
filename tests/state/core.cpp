#include "../support/test.hpp"
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
        const auto expected = CoreState::capture(c);
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
        test::equal(CoreState::capture(c) == expected, true);
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
        test::equal(CoreState::capture(fresh.console) == expected, true);
        test::equal(fresh.audio == expected_audio, true);
      }
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
      bytes[8] = 4;
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
  for (unsigned length : {4u, 8u, 16u, 31u})
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
        put(recipe, 4, 0x20000002);
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
  cartridge->motion(100, -900);
  cartridge->write(0xa080, 0x80);
  cartridge->write(0xa080, 0xc0);
  cartridge->eeprom()[3] = 0x49;
  machine.console.controller(2).sensor().beats_per_minute(130);
  CoreState::restore(machine.console, initial);
  test::equal(CoreState::capture(machine.console) == initial, true);
  machine.console.controller(1).disconnect_pak();
  const auto disconnected = CoreState::capture(machine.console);
  test::equal(rejects(machine.console, initial), true);
  test::equal(CoreState::capture(machine.console) == disconnected, true);
}

} // namespace

void core_state_tests() {
  continuation_tests();
  dispatch_history_tests();
  memory_binding_tests();
  invalid_tests();
  accessory_tests();
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
