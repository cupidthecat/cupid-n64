#include "../fixture.hpp"
#include "core/arcade/aleck64.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;

void arcade_control_tests() {
  for (auto profile :
       {ArcadeProfile::Standard, ArcadeProfile::MagicalTetris, ArcadeProfile::ElevenBeat,
        ArcadeProfile::StarSoldier, ArcadeProfile::HiPai, ArcadeProfile::SuperRealMahjong}) {
    Aleck64 board(profile);
    const auto dips = profile == ArcadeProfile::StarSoldier ? 0x7fcf0000u
                      : profile == ArcadeProfile::HiPai     ? 0xffc70000u
                                                            : 0xffff0000u;
    equal(board.read(0xc0800000, 4).value, dips | 0xffff);
    equal(board.read(0xc0800004, 4).value, 0xffffffff);
    equal(board.read(0xc0800008, 4).value, 0xffffffff);
    equal(board.read(0xc0800100, 4).value, 0);
    auto &input = board.input();
    input.players[0].up = true;
    input.players[1].buttons[0] = true;
    equal(board.read(0xc0800000, 4).value, dips | 0xeffe);
    input.players[0].start = true;
    input.players[1].coin = true;
    input.service = true;
    if (profile == ArcadeProfile::MagicalTetris) {
      equal(board.read(0xc0800000, 4).value, dips | 0xef7e);
      equal(board.read(0xc0800004, 4).value, 0xffffffed);
    } else {
      equal(board.read(0xc0800004, 4).value, 0xffe6ffff);
    }
    std::array<std::uint8_t, 1> command{0};
    std::array<std::uint8_t, 8> response;
    response.fill(0xcc);
    auto status = board.controller(0).communicate(command, response);
    equal(status.valid, true);
    equal(status.overflow, false);
    equal(response[0], 5);
    equal(response[1], 0);
    equal(response[2], 2);
    equal(response[3], 0xcc);
    command[0] = 1;
    input.players[0].x = -85;
    input.players[0].y = 69;
    status = board.controller(0).communicate(command, response);
    equal(status.valid, true);
    equal(status.overflow, true);
    equal(response[0], profile == ArcadeProfile::ElevenBeat ? 0x1f : 0x18);
    equal(response[1], 0);
    equal(response[2], 0xab);
    equal(response[3], 69);
    equal(response[4], 0xcc);
    command[0] = 2;
    equal(board.controller(0).communicate(command, response).valid, false);
    input.players[0].start = false;
    input.mahjong = 1u << 1;
    board.write(0xc0800008, 4, 0x0100);
    const auto expected =
        profile == ArcadeProfile::HiPai || profile == ArcadeProfile::SuperRealMahjong ? 0xfffdffffu
                                                                                      : 0xffffffffu;
    equal(board.read(0xc0800008, 4).value, expected);
    board.power(true);
    equal(board.read(0xc0800008, 4).value, expected);
    board.power();
    equal(board.read(0xc0800008, 4).value, 0xffffffff);
  }
  equal(arcade_profile("hipai2").value() == ArcadeProfile::HiPai, true);
  equal(arcade_profile("srmvsa").value() == ArcadeProfile::SuperRealMahjong, true);
  equal(arcade_profile("mtetrisc").value() == ArcadeProfile::MagicalTetris, true);
  equal(arcade_profile("other").has_value(), false);
  ConsoleConfig config;
  config.arcade_profile = ArcadeProfile::Standard;
  Console console(config);
  auto &input = console.arcade()->input();
  input.players[0].buttons[0] = true;
  input.players[0].x = -85;
  input.players[0].y = -85;
  input.players[1].buttons[1] = true;
  input.players[1].start = true;
  input.players[1].x = 69;
  input.players[1].y = 85;
  auto &pif = console.pif();
  pif.ram()[0] = pif.ram()[7] = 1;
  pif.ram()[1] = pif.ram()[8] = 4;
  pif.ram()[2] = pif.ram()[9] = 1;
  pif.ram()[14] = 0xfe;
  pif.write_word(0x1fc007fc, 1);
  pif.dma_read(0x1fc007c0, 0);
  equal(pif.ram()[3], 0x80);
  equal(pif.ram()[5], 0xab);
  equal(pif.ram()[6], 0xab);
  equal(pif.ram()[10], 0x50);
  equal(pif.ram()[12], 69);
  equal(pif.ram()[13], 85);
  console.connect_controller(1, false);
  pif.dma_read(0x1fc007c0, 0);
  equal(pif.ram()[8], 0x84);
}

} // namespace test
