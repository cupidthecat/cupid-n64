#pragma once
#include "../memory/scenarios.hpp"
namespace test::rsp_replay::nested {
constexpr unsigned branch(unsigned kind, unsigned pc, unsigned target, unsigned indirect) {
  const auto relative = ((target - pc - 4) >> 2) & 0xffff;
  switch (kind) {
  case 0:
    return 0x08000000 | (target >> 2);
  case 1:
    return 0x0c000000 | (target >> 2);
  case 2:
    return indirect << 21 | 8;
  case 3:
    return indirect << 21 | 31u << 11 | 9;
  case 4:
    return 4u << 26 | relative;
  case 5:
    return 5u << 26 | 22u << 21 | relative;
  case 6:
    return 6u << 26 | relative;
  case 7:
    return 7u << 26 | 22u << 21 | relative;
  case 8:
    return 1u << 26 | 23u << 21 | relative;
  case 9:
    return 1u << 26 | 22u << 21 | 1u << 16 | relative;
  case 10:
    return 1u << 26 | 23u << 21 | 16u << 16 | relative;
  case 11:
    return 1u << 26 | 22u << 21 | 17u << 16 | relative;
  case 12:
    return 4u << 26 | 22u << 21 | relative;
  case 13:
    return 5u << 26 | relative;
  case 14:
    return 6u << 26 | 22u << 21 | relative;
  case 15:
    return 7u << 26 | relative;
  case 16:
    return 1u << 26 | 22u << 21 | relative;
  default:
    return 1u << 26 | 23u << 21 | 1u << 16 | relative;
  }
}
template <class P> void scenarios(P &p) {
  for (bool native : {false, true})
    for (unsigned first = 0; first < 18; ++first)
      for (unsigned second = 0; second < 18; ++second)
        for (unsigned start : {0u, 4u, 0xfd0u, 0xffcu}) {
          p.begin(native);
          test::rsp_replay::memory::prepare(p, first, second, first + second, 0x100);
          p.set_reg(22, 1);
          p.set_reg(23, 0xffffffffu);
          p.set_reg(24, (start + 16) & 0xfff);
          p.set_reg(25, (start + 24) & 0xfff);
          p.code(start, branch(first, start, (start + 16) & 0xfff, 24));
          p.code(start + 4, branch(second, (start + 4) & 0xfff, (start + 24) & 0xfff, 25));
          p.code(start + 8, 13);
          p.code(start + 12, 13);
          p.code(start + 16, 0x24210001);
          p.code(start + 20, 0xada10004);
          p.code(start + 24, 13);
          p.set_pc(start);
          p.io_write(16, 8);
          p.io_write(16, 1 | ((first + second) & 1 ? 256 : 0));
          for (unsigned clocks : {1u, 1u, 3u, 9u, 27u, 128u, 4096u}) {
            p.run(clocks);
            p.observe();
          }
          p.finish();
        }
}
} // namespace test::rsp_replay::nested
