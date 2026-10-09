#pragma once

#include "../../support/test.hpp"
#include "core/memory/instruction_tracker.hpp"
#include <span>
#include <string_view>

namespace test::cpu_replay {
inline void mix(std::uint64_t &hash, std::uint64_t value) {
  for (unsigned byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 255;
    hash *= 0x100000001b3ull;
  }
}

struct Memory : cupid::n64::Bus {
  std::vector<std::uint32_t> words = std::vector<std::uint32_t>(2 * 1024 * 1024);
  cupid::n64::InstructionTracker tracker{8 * 1024 * 1024};

  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < words.size() * 4 ? std::span(words).subspan(address / 4)
                                      : std::span<const std::uint32_t>();
  }
  cupid::n64::InstructionTracker *instruction_tracker() override {
    return &tracker;
  }
  cupid::n64::BusRead read(std::uint32_t address, unsigned bytes) override {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < bytes; ++n) {
      const auto a = (address & ~(bytes - 1)) + n;
      value = value << 8 | (a < words.size() * 4 ? (words[a / 4] >> ((3 - (a & 3)) * 8)) & 255 : 0);
    }
    return {value};
  }
  cupid::n64::BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    for (unsigned n = 0; n < bytes; ++n) {
      const auto a = (address & ~(bytes - 1)) + n;
      if (a < words.size() * 4) {
        const auto shift = (3 - (a & 3)) * 8;
        words[a / 4] = (words[a / 4] & ~(255u << shift)) |
                       static_cast<unsigned>((value >> ((bytes - n - 1) * 8)) & 255) << shift;
      }
    }
    tracker.invalidate(address, bytes);
    return {};
  }
  cupid::n64::BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> output) override {
    for (unsigned n = 0; n < output.size(); ++n)
      output[n] = address / 4 + n < words.size() ? words[address / 4 + n] : 0;
    return {};
  }
};

struct Probe {
  Memory bus;
  cupid::n64::RandomGenerator random{0};
  cupid::n64::Cpu cpu{bus, &random};
  std::span<const std::uint64_t> expected;
  std::string_view name;
  std::uint64_t hash = 0xcbf29ce484222325ull, group_hash = hash, observations = 0;
  unsigned cases = 0, groups = 0;

  Probe(std::span<const std::uint64_t> values, std::string_view label)
      : expected(values), name(label) {}
  void begin() {
    cpu.power();
    random.seed(0);
  }
  void gpr(unsigned reg, std::uint64_t value) {
    cpu.state().gpr[reg] = value;
  }
  void fpr(unsigned reg, std::uint64_t value) {
    cpu.state().fpr[reg] = value;
  }
  void hilo(std::uint64_t hi, std::uint64_t lo) {
    cpu.state().hi = hi;
    cpu.state().lo = lo;
  }
  void control(unsigned reg, std::uint64_t value) {
    cpu.write_control(reg, value);
  }
  std::uint64_t read_control(unsigned reg) {
    return cpu.read_control(reg);
  }
  void pc(std::uint64_t value) {
    cpu.set_pc(value);
  }
  void execute(unsigned instruction) {
    cpu.execute(instruction);
  }
  void nmi() {
    cpu.request_nmi();
  }
  void interrupt(unsigned bit, bool pending) {
    cpu.set_interrupt(bit, pending);
  }
  void single(bool native) {
    if (!native || !cpu.run_block(cpu.state().clocks + 1))
      cpu.step();
  }
  void mapping(unsigned mode) {
    control(12, 0x10000000);
    control(0, 1);
    control(2, (0x1000 >> 6) | 0x1f);
    control(3, (0x2000 >> 6) | 0x1f);
    control(5, 0);
    control(10, 0x40000);
    execute(0x42000002);
    control(12, mode);
  }
  void code(unsigned offset, unsigned value) {
    bus.write(0x1000 + offset, 4, value);
  }
  std::uint32_t word(unsigned offset) {
    return bus.words[(0x1000 + offset) / 4];
  }
  void physical_write(unsigned address, unsigned value) {
    bus.write(address, 4, value);
  }
  std::uint32_t physical_read(unsigned address) {
    return bus.words[address / 4];
  }
  std::uint64_t reg(unsigned index) {
    return cpu.state().gpr[index];
  }
  void run(bool native, std::uint64_t start, std::uint64_t end) {
    if (native) {
      if (!cpu.run_block(cpu.state().clocks + 1))
        cpu.step();
    } else {
      for (unsigned n = 0; n < 32 && cpu.state().pc >= start && cpu.state().pc < end; ++n)
        cpu.step();
    }
  }
  void observe() {
    const auto &s = cpu.state();
    for (auto value : s.gpr)
      emit(value);
    for (auto value : s.fpr)
      emit(value);
    emit(s.hi);
    emit(s.lo);
    emit(s.pc);
    emit(s.clocks);
    emit(s.fcr31);
    emit(cpu.in_delay_slot());
    for (unsigned reg = 0; reg < 32; ++reg)
      emit(cpu.read_control(reg));
  }
  void emit(std::uint64_t value) {
    mix(hash, value);
    ++observations;
  }
  void finish() {
    mix(group_hash, hash);
    hash = 0xcbf29ce484222325ull;
    if (++cases % 16)
      return;
    equal(groups < expected.size(), true);
    if (groups < expected.size()) {
      if (group_hash != expected[groups])
        std::cerr << name << " cases " << cases - 16 << '-' << cases - 1 << '\n';
      equal(group_hash, expected[groups]);
    }
    ++groups;
    group_hash = 0xcbf29ce484222325ull;
  }
  void complete(unsigned count, std::uint64_t observation_count) {
    equal(cases, count);
    equal(cases % 16, 0);
    equal(groups, expected.size());
    equal(observations, observation_count);
  }
};
} // namespace test::cpu_replay
