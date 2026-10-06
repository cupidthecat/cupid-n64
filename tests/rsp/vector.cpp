#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_vector_tests() {
  RspFixture f;
  auto &rsp = f.rsp;
  auto &state = rsp.state();
  auto &a = state.vectors[1].lanes;
  auto &b = state.vectors[2].lanes;
  auto &result = state.vectors[3].lanes;
  for (unsigned n = 0; n < 8; ++n)
    b[n] = static_cast<std::uint16_t>(n + 1);
  constexpr unsigned indices[16][8] = {
      {0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 0, 2, 2, 4, 4, 6, 6},
      {1, 1, 3, 3, 5, 5, 7, 7}, {0, 0, 0, 0, 4, 4, 4, 4}, {1, 1, 1, 1, 5, 5, 5, 5},
      {2, 2, 2, 2, 6, 6, 6, 6}, {3, 3, 3, 3, 7, 7, 7, 7}, {0, 0, 0, 0, 0, 0, 0, 0},
      {1, 1, 1, 1, 1, 1, 1, 1}, {2, 2, 2, 2, 2, 2, 2, 2}, {3, 3, 3, 3, 3, 3, 3, 3},
      {4, 4, 4, 4, 4, 4, 4, 4}, {5, 5, 5, 5, 5, 5, 5, 5}, {6, 6, 6, 6, 6, 6, 6, 6},
      {7, 7, 7, 7, 7, 7, 7, 7}};
  for (unsigned element = 0; element < 16; ++element) {
    rsp.execute(vector(0x2a, 3, 1, 2, element));
    for (unsigned lane = 0; lane < 8; ++lane) {
      equal(result[lane], indices[element][lane] + 1);
      equal(state.accumulator.get(lane), result[lane]);
    }
  }
  a.fill(0xffff);
  b.fill(1);
  rsp.execute(vector(0x14, 3, 1, 2));
  equal(state.carry_low, 255);
  equal(state.carry_high, 0);
  equal(result[0], 0);
  a.fill(0x7fff);
  b.fill(0);
  rsp.execute(vector(0x10, 3, 1, 2));
  equal(result[0], 0x7fff);
  equal(state.accumulator.get(0), 0x8000);
  equal(state.carry_low, 0);
  a.fill(0x8000);
  b.fill(0x8000);
  rsp.execute(vector(0, 3, 1, 2));
  equal(state.accumulator.get(0), 0x80008000);
  equal(result[0], 0x7fff);
  rsp.execute(vector(1, 3, 1, 2));
  equal(result[0], 0xffff);
  a.fill(0xffff);
  b.fill(1);
  rsp.execute(vector(7, 3, 1, 2));
  equal(state.accumulator.get(0), 0xffffffff0000);
  equal(result[0], 0xffff);
  rsp.execute(vector(0x1d, 3, 0, 0, 8));
  equal(result[0], 0xffff);
  rsp.execute(vector(0x1d, 3, 0, 0, 9));
  equal(result[0], 0xffff);
  rsp.execute(vector(0x1d, 3, 0, 0, 10));
  equal(result[0], 0);
  a.fill(0x8000);
  b.fill(0x7fff);
  rsp.execute(vector(0x25, 3, 1, 2));
  equal(result[0], 0x8001);
  equal(state.carry_low, 255);
  equal(state.carry_high, 0);
  equal(state.compare_low, 255);
  equal(state.compare_high, 0);
  equal(state.extension, 255);
  rsp.execute(vector(0x24, 3, 1, 2));
  equal(result[0], 0x8001);
  equal(state.carry_low, 0);
  equal(state.extension, 0);
  rsp.execute(vector(0x26, 3, 1, 2));
  equal(result[0], 0x8000);
  a.fill(123);
  b.fill(123);
  state.carry_low = state.carry_high = 255;
  rsp.execute(vector(0x20, 3, 1, 2));
  equal(state.compare_low, 255);
  state.carry_low = state.carry_high = 255;
  rsp.execute(vector(0x23, 3, 1, 2));
  equal(state.compare_low, 0);
  a.fill(0xffff);
  b.fill(0x8000);
  rsp.execute(vector(0x13, 3, 1, 2));
  equal(result[0], 0x7fff);
  equal(state.accumulator.get(0) & 0xffff, 0x8000);

  b.fill(1);
  rsp.execute(vector(0x30, 3, 0, 2));
  equal(result[0], 0xc000);
  equal(state.divide_output, 0x7fff);
  b.fill(0);
  rsp.execute(vector(0x32, 3, 1, 2));
  equal(result[1], 0x7fff);
  equal(state.divide_double, true);
  b.fill(2);
  rsp.execute(vector(0x31, 3, 2, 2));
  equal(result[2], 0xe000);
  equal(state.divide_output, 0x3fff);
  equal(state.divide_double, false);
  b.fill(0x8000);
  rsp.execute(vector(0x34, 3, 3, 2));
  equal(result[3], 0);
  equal(state.divide_output, 0xffff);
  b.fill(0);
  rsp.execute(vector(0x34, 3, 4, 2));
  equal(result[4], 0xffff);
  equal(state.divide_output, 0x7fff);
  b.fill(1);
  rsp.execute(vector(0x34, 3, 5, 2));
  equal(result[5], 0xc000);

  state.gpr[1] = 0xabcd;
  rsp.execute((18u << 26) | (4u << 21) | (1u << 16) | (2u << 11) | (15u << 7));
  equal(state.vectors[2].byte(15), 0xab);
  equal(state.vectors[2].byte(0), 0);
  rsp.execute((18u << 26) | (1u << 16) | (2u << 11) | (15u << 7));
  equal(state.gpr[1], 0xffffab00);
  state.gpr[1] = 0xfedc;
  rsp.execute((18u << 26) | (6u << 21) | (1u << 16));
  equal(state.carry_low, 0xdc);
  equal(state.carry_high, 0xfe);
  rsp.execute((18u << 26) | (2u << 21) | (1u << 16));
  equal(state.gpr[1], 0xfffffedc);
}

} // namespace test
