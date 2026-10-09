#include "../fixture.hpp"
#include "core/controller/gamepad.hpp"
#include "core/controller/stick/response.hpp"
#include <cmath>

namespace test {
using namespace cupid::n64;
namespace {

struct Case {
  std::int16_t x, y;
  std::uint16_t report;
};

constexpr Case cases[]{
    {0, 0, 0x0000},          {32767, 0, 0x5500},       {-32767, 0, 0xab00},
    {-32768, 0, 0xab00},     {0, 32767, 0x00ab},       {0, -32767, 0x0055},
    {0, -32768, 0x0055},     {32767, 32767, 0x45bb},   {-32767, 32767, 0xbbbb},
    {32767, -32767, 0x4545}, {-32767, -32767, 0xbb45}, {-32768, -32768, 0xbb45},
    {2277, 0, 0x0000},       {2278, 0, 0x0000},        {2580, 0, 0x0000},
    {2581, 0, 0x0100},       {-2277, 0, 0x0000},       {-2278, 0, 0x0000},
    {-2580, 0, 0x0000},      {-2581, 0, 0xff00},       {0, 2277, 0x0000},
    {0, 2278, 0x0000},       {0, 2580, 0x0000},        {0, 2581, 0x00ff},
    {0, -2277, 0x0000},      {0, -2278, 0x0000},       {0, -2580, 0x0000},
    {0, -2581, 0x0001},      {16384, 0, 0x2e00},       {0, 16384, 0x00d2},
    {16384, 16384, 0x2ed2},  {23169, 23169, 0x44bc},   {23170, 23170, 0x45bb},
    {32767, 8192, 0x51f1},   {8192, 32767, 0x0faf},    {32767, 16384, 0x4cdd},
    {16384, 32767, 0x23b4},  {32767, 24576, 0x48cb},   {24576, 32767, 0x35b8},
    {-32767, 8192, 0xaff1},  {8192, -32767, 0x0f51},   {-16384, 32767, 0xddb4},
    {32767, -16384, 0x4c23}, {2000, 32767, 0x00ab},    {32767, 2000, 0x5500},
    {12000, 4000, 0x20fb},   {-12000, -4000, 0xe005},  {19991, 0, 0x3a00},
};

} // namespace

void stick_tests() {
  RandomGenerator random;
  Gamepad pad(random);
  for (const auto &row : cases) {
    const auto position = stick_response(row.x, row.y);
    equal(static_cast<std::uint8_t>(position.x), row.report >> 8);
    equal(static_cast<std::uint8_t>(-position.y), row.report & 255);
    pad.input_host(0, row.x, row.y);
    equal(pad.state(), row.report);
    std::array<std::uint8_t, 1> command{1};
    std::array<std::uint8_t, 4> report{};
    const auto status = pad.communicate(command, report);
    equal(status.valid, true);
    equal(status.overflow, false);
    equal(report[0], 0);
    equal(report[1], 0);
    equal(report[2], row.report >> 8);
    equal(report[3], row.report & 255);
  }
  for (unsigned buttons : {0x8010u, 0x0f00u, 0x1030u, 0xffffu}) {
    constexpr std::array<std::uint32_t, 4> expected{0x80102e2e, 0x00002e2e, 0x00b00000, 0xe0bf0000};
    const auto index = buttons == 0x8010 ? 0 : buttons == 0x0f00 ? 1 : buttons == 0x1030 ? 2 : 3;
    pad.input_host(static_cast<std::uint16_t>(buttons), 16384, -16384);
    equal(pad.state(), expected[index]);
  }
  pad.input(0x8010, 100, -110);
  equal(pad.state(), 0x80106492);
  for (int x = 0; x <= 32767; ++x) {
    const auto positive =
        stick_response(static_cast<std::int16_t>(x), static_cast<std::int16_t>(x / 2));
    const auto negative =
        stick_response(static_cast<std::int16_t>(-x), static_cast<std::int16_t>(-x / 2));
    equal(negative.x == -positive.x, true);
    equal(negative.y == -positive.y, true);
    const int ax = std::abs(int(positive.x)), ay = std::abs(int(positive.y));
    equal(ax <= 85 && ay <= 85, true);
    equal(69 * ax + 16 * ay <= 85 * 69, true);
    equal(16 * ax + 69 * ay <= 85 * 69, true);
  }
}

} // namespace test
