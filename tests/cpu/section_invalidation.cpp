#include "../support/test.hpp"
#include "core/memory/instruction_tracker.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct TrackedMemory : Bus {
  std::array<std::uint32_t, 4096> words{};
  InstructionTracker tracker{sizeof(words)};
  bool tracked = true;
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address / 4)
                                   : std::span<const std::uint32_t>();
  }
  InstructionTracker *instruction_tracker() override {
    return tracked ? &tracker : nullptr;
  }
  BusRead read(std::uint32_t address, unsigned bytes) override {
    address &= ~(bytes - 1u);
    const auto word = words[(address / 4) % words.size()];
    if (bytes == 8)
      return {(std::uint64_t(word) << 32) | words[(address / 4 + 1) % words.size()]};
    return {(word >> ((4 - bytes - (address & 3)) * 8)) & ((1ull << (bytes * 8)) - 1)};
  }
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    address &= ~(bytes - 1u);
    for (unsigned n = 0; n < bytes; ++n) {
      auto &word = words[((address + n) / 4) % words.size()];
      const auto shift = (3 - ((address + n) & 3)) * 8;
      word = (word & ~(255u << shift)) |
             (static_cast<std::uint32_t>((value >> ((bytes - 1 - n) * 8)) & 255) << shift);
    }
    tracker.invalidate(address, bytes);
    return {};
  }
};

struct TrackedFixture {
  TrackedMemory memory;
  Cpu cpu{memory};
  bool little;
  explicit TrackedFixture(bool little) : little(little) {
    configure();
  }
  void configure() {
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
    cpu.state().gpr[5] = 0x30000000;
  }
  void code(unsigned address, std::uint32_t word) {
    memory.write(address ^ (little ? 4u : 0u), 4, word);
  }
  void invalidate(unsigned address) {
    cpu.state().gpr[30] = 0xffffffff80000000 | address;
    cpu.execute(i(47, 30, 16, 0));
  }
  void start(unsigned address) {
    cpu.set_pc(0xffffffff80000000 | address);
  }
};

void run(TrackedFixture &actual, TrackedFixture &expected, unsigned count) {
  for (unsigned n = 0; n < count; ++n)
    expected.cpu.step();
  for (unsigned n = 0; n < count + 8 && actual.cpu.state().pc != expected.cpu.state().pc; ++n) {
    const auto budget = actual.cpu.state().clocks;
    if (!actual.cpu.run_block(budget))
      actual.cpu.step();
  }
  equal(actual.cpu.state().pc, expected.cpu.state().pc);
  equal(actual.cpu.in_delay_slot(), expected.cpu.in_delay_slot());
  for (unsigned n = 0; n < 32; ++n) {
    equal(actual.cpu.state().gpr[n], expected.cpu.state().gpr[n]);
    if (n != Count)
      equal(actual.cpu.read_control(n), expected.cpu.read_control(n));
  }
  equal(actual.memory.words == expected.memory.words, true);
}

} // namespace

void section_invalidation_tests() {
  for (bool little : {false, true}) {
    for (unsigned base : {0x1000u, 0x1fe0u}) {
      for (unsigned changed : {0u, 7u, 8u, 11u}) {
        for (unsigned width : {1u, 2u, 4u, 8u}) {
          TrackedFixture actual(little), expected(little);
          for (auto *f : {&actual, &expected}) {
            for (unsigned n = 0; n < 12; ++n)
              f->code(base + n * 4, i(9, 4, 4, 1));
            f->code(base + 48, c(4, 5, Status));
            f->start(base);
          }
          run(actual, expected, 13);
          for (unsigned pass = 0; pass < 4; ++pass) {
            for (auto *f : {&actual, &expected}) {
              const auto address = (base + changed * 4) ^ (little ? 4u : 0u);
              const auto aligned = address & ~(width - 1u);
              if (pass == 0) {
                auto value = f->memory.read(aligned, width).value;
                const auto byte = address + 3;
                const auto shift = (width - 1 - (byte - aligned)) * 8;
                if (width < 4)
                  f->memory.write(address + 4 - width, width, 7);
                else
                  f->memory.write(aligned, width, (value & ~(255ull << shift)) | (7ull << shift));
              }
              if (pass == 1)
                for (unsigned offset = 0; offset <= 48; offset += 32)
                  f->invalidate(base + offset);
              if (pass == 2) {
                f->cpu.power();
                f->configure();
              }
              f->start(base);
            }
            run(actual, expected, 13);
          }
        }
      }
    }
    // A store to an unfetched line must change the active block's later instruction.
    for (bool cached : {false, true}) {
      TrackedFixture actual(little), expected(little);
      for (auto *f : {&actual, &expected}) {
        f->code(0x101c, i(43, 1, 2, 0));
        f->code(0x1020, i(9, 4, 4, 1));
        f->code(0x1024, c(4, 5, Status));
        f->cpu.state().gpr[1] = cached ? 0xffffffff80001020 : 0xffffffffa0001020;
        f->cpu.state().gpr[2] = i(9, 4, 4, 7);
        f->start(0x101c);
      }
      run(actual, expected, 3);
      if (cached) {
        for (auto *f : {&actual, &expected}) {
          f->cpu.state().gpr[30] = 0xffffffff80001020;
          f->cpu.execute(i(47, 30, 21, 0));
          f->invalidate(0x1020);
          f->start(0x1020);
        }
        run(actual, expected, 2);
      }
    }
    // Entering an unchanged suffix cannot validate a modified prefix of its owner.
    TrackedFixture actual(little), expected(little);
    for (auto *f : {&actual, &expected}) {
      f->code(0x1000, i(9, 4, 4, 1));
      f->code(0x1004, i(4, 1, 0, 14));
      f->code(0x1008, 0);
      f->code(0x1040, i(9, 6, 6, 1));
      f->code(0x1044, c(4, 5, Status));
      f->start(0x1000);
    }
    run(actual, expected, 3);
    run(actual, expected, 2);
    for (auto *f : {&actual, &expected}) {
      f->code(0x1000, i(9, 4, 4, 7));
      f->start(0x1040);
    }
    run(actual, expected, 2);
    for (auto *f : {&actual, &expected})
      f->start(0x1000);
    run(actual, expected, 3);
    for (auto *f : {&actual, &expected}) {
      f->invalidate(0x1000);
      f->start(0x1000);
    }
    run(actual, expected, 3);
    // Untracked writes are checked on dispatch, including entries compiled while suspended.
    for (auto *f : {&actual, &expected}) {
      f->memory.tracked = false;
      f->memory.words[(0x1040 / 4) ^ unsigned(little)] = i(9, 6, 6, 9);
      f->invalidate(0x1040);
      f->start(0x1040);
    }
    run(actual, expected, 2);
    for (auto *f : {&actual, &expected}) {
      f->memory.tracked = true;
      f->start(0x1040);
    }
    run(actual, expected, 2);
  }
}

} // namespace test
