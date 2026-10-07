#include "native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;

namespace {

struct Beat {
  unsigned offset, bytes, shift;
};

class StoreMemory : public native_memory::CachedMemory {
public:
  unsigned writes = 0, fail_at = 0;

  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    if (++writes == fail_at) {
      transfers.push_back({address, bytes, true, value});
      return {clocks, false};
    }
    return Memory::write(address, bytes, value);
  }
};

struct StoreFixture {
  StoreMemory memory;
  Cpu cpu{memory};

  void run(unsigned operation, unsigned offset, bool little, bool native,
           std::uint64_t address = 0xffffffffa0002000) {
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
    cpu.state().gpr[1] = address;
    cpu.state().gpr[2] = 0x0123456789abcdef;
    cpu.state().gpr[5] = 0x30000000;
    const auto instruction = i(operation, 1, 2, static_cast<std::uint16_t>(offset));
    if (native) {
      cpu.set_pc(0xffffffff80001000);
      memory.words[1024 ^ unsigned(little)] = instruction;
      memory.words[1025 ^ unsigned(little)] = c(4, 5, Status);
      equal(cpu.run_block(cpu.state().clocks), true);
    } else {
      cpu.set_pc(0xffffffffa0001000);
      cpu.execute(instruction);
    }
  }
};

void check_beats(unsigned operation, bool little, std::span<const std::vector<Beat>> cases) {
  for (bool native : {false, true}) {
    for (unsigned offset = 0; offset < cases.size(); ++offset) {
      for (unsigned fail_at = 0; fail_at <= cases[offset].size(); ++fail_at) {
        StoreFixture fixture;
        fixture.memory.fail_at = fail_at;
        fixture.run(operation, offset, little, native);
        const auto count = fail_at ? fail_at : cases[offset].size();
        equal(fixture.memory.writes, count);
        equal((fixture.cpu.read_control(Cause) >> 2) & 31, fail_at ? 7 : 0);
        unsigned checked = 0;
        for (const auto &transfer : fixture.memory.transfers) {
          if (!transfer.store)
            continue;
          if (checked >= count) {
            equal(checked < count, true);
            continue;
          }
          const auto beat = cases[offset][checked++];
          equal(transfer.address, (0x2000 + beat.offset) ^ (little ? 8 - beat.bytes : 0));
          equal(transfer.bytes, beat.bytes);
          const auto mask = beat.bytes == 8 ? ~0ull : (1ull << (beat.bytes * 8)) - 1;
          equal(transfer.value & mask, (0x0123456789abcdefull >> beat.shift) & mask);
        }
        equal(checked, count);
        std::array<std::uint8_t, 8> expected{};
        const auto completed = fail_at ? fail_at - 1 : count;
        for (unsigned n = 0; n < completed; ++n) {
          const auto beat = cases[offset][n];
          const auto address = (beat.offset ^ (little ? 8 - beat.bytes : 0)) & ~(beat.bytes - 1);
          const auto value = 0x0123456789abcdefull >> beat.shift;
          for (unsigned lane = 0; lane < beat.bytes; ++lane)
            expected[address + lane] =
                static_cast<std::uint8_t>(value >> ((beat.bytes - 1 - lane) * 8));
        }
        for (unsigned lane = 0; lane < expected.size(); ++lane)
          equal(fixture.memory.get(0x2000 + lane, 1), expected[lane]);
      }
    }
  }
}

} // namespace

void merge_store_tests() {
  const std::vector<Beat> right64[] = {
      {{0, 1, 0}},
      {{0, 2, 0}},
      {{0, 2, 8}, {2, 1, 0}},
      {{0, 4, 0}},
      {{0, 4, 8}, {4, 1, 0}},
      {{0, 4, 16}, {4, 2, 0}},
      {{0, 4, 24}, {4, 2, 8}, {6, 1, 0}},
      {{0, 8, 0}},
  };
  check_beats(0x2d, false, right64);

  const std::vector<Beat> right32[] = {
      {{0, 1, 0}}, {{1, 2, 0}}, {{2, 1, 0}, {0, 2, 8}}, {{3, 4, 0}}};
  check_beats(0x2e, false, right32);

  const std::vector<Beat> left32[] = {
      {{0, 4, 0}}, {{1, 1, 24}, {2, 2, 8}}, {{2, 2, 16}}, {{3, 1, 24}}};
  check_beats(0x2a, false, left32);
  const std::vector<Beat> left32_little[] = {
      {{0, 1, 24}}, {{0, 2, 16}}, {{2, 1, 24}, {0, 2, 8}}, {{0, 4, 0}}};
  check_beats(0x2a, true, left32_little);
  const std::vector<Beat> right32_little[] = {
      {{0, 4, 0}}, {{2, 2, 8}, {1, 1, 0}}, {{2, 2, 0}}, {{3, 1, 0}}};
  check_beats(0x2e, true, right32_little);

  const std::vector<Beat> left64[] = {
      {{0, 8, 0}},
      {{1, 1, 56}, {2, 2, 40}, {4, 4, 8}},
      {{2, 2, 48}, {4, 4, 16}},
      {{3, 1, 56}, {4, 4, 24}},
      {{4, 4, 32}},
      {{5, 1, 56}, {6, 2, 40}},
      {{6, 2, 48}},
      {{7, 1, 56}},
  };
  check_beats(0x2c, false, left64);
  const std::vector<Beat> left64_little[] = {
      {{0, 1, 56}},
      {{0, 2, 48}},
      {{2, 1, 56}, {0, 2, 40}},
      {{0, 4, 32}},
      {{4, 1, 56}, {0, 4, 24}},
      {{4, 2, 48}, {0, 4, 16}},
      {{6, 1, 56}, {4, 2, 40}, {0, 4, 8}},
      {{0, 8, 0}},
  };
  check_beats(0x2c, true, left64_little);
  const std::vector<Beat> right64_little[] = {
      {{0, 8, 0}},
      {{4, 4, 24}, {2, 2, 8}, {1, 1, 0}},
      {{4, 4, 16}, {2, 2, 0}},
      {{4, 4, 8}, {3, 1, 0}},
      {{4, 4, 0}},
      {{6, 2, 8}, {5, 1, 0}},
      {{6, 2, 0}},
      {{7, 1, 0}},
  };
  check_beats(0x2d, true, right64_little);

  for (bool native : {false, true}) {
    for (unsigned operation : {0x2du, 0x2eu}) {
      const auto bytes = operation == 0x2d ? 8u : 4u;
      for (unsigned offset = 0; offset < bytes; ++offset) {
        StoreFixture fixture;
        fixture.run(operation, offset, false, native, 0x2000);
        equal((fixture.cpu.read_control(Cause) >> 2) & 31, 3);
        equal(fixture.cpu.read_control(BadVAddr), 0x2000 + (operation == 0x2e ? offset : 0));
        equal(fixture.memory.writes, 0);
      }
    }
  }

  for (bool native : {false, true}) {
    for (bool little : {false, true}) {
      for (unsigned operation : {0x2au, 0x2cu, 0x2du}) {
        const auto bytes = operation == 0x2a ? 4u : 8u;
        for (unsigned offset = 0; offset < bytes; ++offset) {
          StoreFixture fixture;
          fixture.run(operation, offset, little, native, 0x00000000a0002000);
          equal((fixture.cpu.read_control(Cause) >> 2) & 31, 5);
          equal(fixture.memory.writes, 0);
          equal(fixture.memory.get(0x2000, 8), 0);
        }
      }
    }
  }
}

} // namespace test
