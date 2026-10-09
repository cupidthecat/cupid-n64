#pragma once

#include "../fixture.hpp"
#include "core/rsp/vector/execute.hpp"
#include <span>
#include <string_view>

namespace test::rsp_replay {
inline void mix(std::uint64_t &hash, std::uint64_t value) {
  for (unsigned byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 255;
    hash *= 0x100000001b3ull;
  }
}

struct Probe {
  RspFixture f;
  std::span<const std::uint64_t> expected;
  std::string_view name;
  std::uint64_t hash = 0xcbf29ce484222325ull, group_hash = hash, observations = 0;
  unsigned cases = 0, groups = 0, route = 0;
  bool native = false;

  Probe(std::span<const std::uint64_t> values, std::string_view label)
      : expected(values), name(label) {
    f.initialize();
  }
  void begin(bool value) {
    native = value;
    f.rsp.power();
  }
  void mode(unsigned value) {
    route = value;
  }
  void set_reg(unsigned reg, unsigned value) {
    f.rsp.state().gpr[reg] = value;
  }
  void set_vector(unsigned reg, unsigned lane, unsigned value) {
    f.rsp.state().vectors[reg].lanes[lane] = static_cast<std::uint16_t>(value);
  }
  void set_accumulator(unsigned lane, std::uint64_t value) {
    f.rsp.state().accumulator.set(lane, value);
  }
  void set_flags(std::uint64_t flags, unsigned input, unsigned output, bool pending) {
    auto &s = f.rsp.state();
    s.carry_low = static_cast<std::uint8_t>(flags);
    s.carry_high = static_cast<std::uint8_t>(flags >> 8);
    s.compare_low = static_cast<std::uint8_t>(flags >> 16);
    s.compare_high = static_cast<std::uint8_t>(flags >> 24);
    s.extension = static_cast<std::uint8_t>(flags >> 32);
    s.divide_input = static_cast<std::uint16_t>(input);
    s.divide_output = static_cast<std::uint16_t>(output);
    s.divide_double = pending;
  }
  void local_write(unsigned address, std::uint64_t value) {
    f.rsp.write_local(address, 8, value);
  }
  void code(unsigned address, unsigned instruction) {
    f.rsp.write_local(0x1000 | (address & 0xfff), 4, instruction);
  }
  void set_pc(unsigned address) {
    f.rsp.write_status(0, address);
  }
  void io_write(unsigned address, unsigned value) {
    f.rsp.write_io(address, value);
  }
  void run(unsigned clocks) {
    f.rsp.elapse(clocks);
    if (native)
      f.rsp.run();
    else
      f.rsp.run_interpreted();
  }
  void execute(unsigned instruction) {
    auto &s = f.rsp.state();
    const auto operation = instruction & 63;
    if (route == 2) {
      f.rsp.execute(instruction);
      return;
    }
    if (operation >= 0x30 && operation <= 0x36) {
      const auto handler = cupid::n64::vector_divide_handler(operation, (instruction >> 21) & 15);
      handler(&s, &s.vectors[(instruction >> 6) & 31], &s.vectors[(instruction >> 11) & 31],
              &s.vectors[(instruction >> 16) & 31]);
    } else if (!route || !cupid::n64::execute_vector_simd(s, instruction))
      cupid::n64::execute_vector_scalar(s, instruction);
  }
  void emit(std::uint64_t value) {
    mix(hash, value);
    ++observations;
  }
  void registers() {
    const auto &s = f.rsp.state();
    for (auto value : s.gpr)
      emit(value);
    for (const auto &reg : s.vectors)
      for (auto value : reg.lanes)
        emit(value);
    for (unsigned lane = 0; lane < 8; ++lane)
      emit(s.accumulator.get(lane));
    emit(s.carry_low);
    emit(s.carry_high);
    emit(s.compare_low);
    emit(s.compare_high);
    emit(s.extension);
    emit(s.divide_input);
    emit(s.divide_output);
    emit(s.divide_double);
  }
  void memory() {
    for (unsigned address = 0; address < 4096; address += 8)
      emit(f.rsp.read_local(address, 8));
  }
  void status() {
    emit(f.rsp.pc());
    emit(static_cast<std::uint64_t>(f.rsp.clocks()));
    emit(f.rsp.read_io(16));
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
} // namespace test::rsp_replay
