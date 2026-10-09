#pragma once
#include "../arithmetic/scenarios.hpp"
#include "../memory/scenarios.hpp"
#include <array>
namespace test::rsp_replay::pipeline {
constexpr std::uint32_t integer(unsigned op, unsigned source, unsigned target, unsigned immediate) {
  return op << 26 | source << 21 | target << 16 | (immediate & 0xffff);
}
constexpr std::uint32_t transfer(unsigned op, unsigned reg, unsigned target, unsigned element) {
  return 18u << 26 | op << 21 | reg << 16 | target << 11 | element << 7;
}
constexpr std::uint32_t memory(bool store, unsigned op, unsigned target, unsigned element) {
  return (store ? 58u : 50u) << 26 | 13u << 21 | target << 16 | op << 11 | element << 7;
}
constexpr auto vector(unsigned op, unsigned dest, unsigned source, unsigned target,
                      unsigned element = 0) {
  return test::rsp_replay::arithmetic::opcode(op, dest, source, target, element);
}
template <class P> void scenarios(P &p) {
  constexpr std::array registers{0u, 1u, 2u, 7u, 15u, 16u, 30u, 31u};
  for (bool native : {false, true})
    for (unsigned family = 0; family < 20; ++family)
      for (unsigned start : {0u, 4u, 0xfd0u, 0xffcu})
        for (unsigned trial = 0; trial < 8; ++trial) {
          p.begin(native);
          test::rsp_replay::memory::prepare(p, family, trial, trial, 0xff8 + trial);
          p.io_write(16, 8);
          const auto reg = registers[trial], next = (reg + 1) & 31;
          const auto v = (trial * 3) & 31, a = (v + 1) & 31, b = (v + 8) & 31;
          const auto load = integer(35, 13, reg, 0), read = integer(9, reg, next, 1);
          const auto store = integer(43, 13, reg, 4), nop = 0u;
          std::array<unsigned, 16> words{};
          switch (family) {
          case 0:
            words = {load, read, store, 13};
            break;
          case 1:
            words = {load, nop, read, store, 13};
            break;
          case 2:
            words = {load, nop, nop, read, store, 13};
            break;
          case 3:
            words = {load, store, store, 13};
            break;
          case 4:
            words = {vector(0, v, a, b), vector(0x10, b, v, a), 13};
            break;
          case 5:
            words = {vector(0, v, a, b), nop, vector(0x10, b, v, a), 13};
            break;
          case 6:
            words = {vector(0, v, a, b), nop, nop, vector(0x10, b, v, a), 13};
            break;
          case 7:
            words = {vector(0x2a, v, a, b), integer(9, reg, next, 1), 13};
            break;
          case 8:
            words = {transfer(4, reg, v, trial), vector(0x37, v, 0, 0), 13};
            break;
          case 9:
            words = {transfer(4, reg, v, trial), vector(0x37, a, 0, 0), 13};
            break;
          case 10:
            words = {memory(false, 11, v, trial), vector(0x37, v, 0, 0), 13};
            break;
          case 11:
            words = {memory(false, 11, v, trial), vector(0x37, b, 0, 0), 13};
            break;
          case 12:
            words = {memory(false, 4, v, trial), transfer(0, reg, v, trial), read,
                     memory(true, 4, v, trial), 13};
            break;
          case 13:
            words = {transfer(6, reg, 0, 0), vector(0x10, v, a, b), transfer(2, next, 0, 0), 13};
            break;
          case 14:
            words = {load, integer(4, reg, 0, 2), read, store, 13};
            break;
          case 15:
            words = {0x10000002u, read, store, 13};
            break;
          case 16:
            words = {load, 0x40800000u | 29u << 16 | 4u << 11, read, store, 13};
            p.set_reg(29, 2);
            break;
          case 17:
            words = {load, 13, vector(0x10, v, a, b), 13};
            break;
          case 18:
            words = {0x08000000u | (((start + 16) & 0xfff) >> 2),
                     0x08000000u | (((start + 24) & 0xfff) >> 2),
                     13,
                     13,
                     read,
                     store,
                     13};
            break;
          case 19:
            words = {vector(0x2a, v, a, b), integer(4, 0, 0, 2), read, store, 13};
            break;
          }
          for (unsigned index = 0; index < words.size(); ++index)
            p.code(start + index * 4, words[index]);
          p.set_pc(start);
          p.io_write(16, 1 | ((trial & 1) ? 256u : 0u));
          for (unsigned clocks : {1u, 1u, 3u, 9u, 27u, 128u, 4096u}) {
            p.run(clocks);
            p.observe();
          }
          if (family == 16) {
            p.io_write(16, 1);
            p.run(4096);
            p.observe();
          }
          p.finish();
        }
  for (bool native : {false, true})
    for (unsigned start : {0u, 4u, 0xff0u, 0xffcu})
      for (auto reg : registers) {
        p.begin(native);
        test::rsp_replay::memory::prepare(p, reg, 0, reg, 0x108);
        p.io_write(16, 8);
        const auto first = integer(9, reg, 31, 1);
        p.code(start, first);
        p.code(start + 4, 0x08000000u | (start >> 2));
        p.code(start + 8, integer(35, 13, reg, 0));
        p.set_pc(start);
        p.io_write(16, 1);
        for (unsigned stage = 0; stage < 8; ++stage) {
          if (stage == 2)
            p.code(start, first);
          if (stage == 3)
            p.code(start + 0x100, 0);
          if (stage == 4)
            p.local_write(0x108, 0x123456789abcdef0ull);
          if (stage == 5)
            p.code(start, first + 1);
          if (stage == 6)
            p.code(start, first);
          p.run(1);
          p.observe();
          p.run(32);
          p.observe();
        }
        p.finish();
      }
}
} // namespace test::rsp_replay::pipeline
