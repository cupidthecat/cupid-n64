#include "core/controller/gamepad.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void gamepad_tests() {
  RandomGenerator random;
  Gamepad pad(random);
  pad.input(0x8010, 69, -69);
  equal(pad.state(), 0x801045bb);
  pad.input(0x0f00, -85, 85);
  equal(pad.state(), 0x0000ab55);
  pad.input(0x1030, 60, -20);
  equal(pad.state(), 0x00b00000);
  std::array<std::uint8_t, 1> command{0};
  std::array<std::uint8_t, 3> status{};
  equal(pad.communicate(command, status).valid, true);
  equal(status[0], 5);
  equal(status[2], 2);
  pad.memory_pak(4);
  equal(pad.pak_data().size(), 4 * 0x8000);
  pad.communicate(command, status);
  equal(status[2], 3);
  pad.communicate(command, status);
  equal(status[2], 1);
  equal(pad.pak_data()[0x3a], 4);
  equal(pad.pak_data()[0x100 + 11 * 2 + 1], 3);
  equal(pad.pak_data()[0x100 + 10 * 2 + 1], 0);
  std::array<std::uint8_t, 35> write{3, 0x10, static_cast<std::uint8_t>(address_crc(0x1000))};
  for (unsigned n = 3; n < write.size(); ++n)
    write[n] = static_cast<std::uint8_t>(n);
  std::array<std::uint8_t, 1> response{};
  equal(pad.communicate(write, response).valid, true);
  equal(response[0], data_crc(std::span<const std::uint8_t, 32>(write.data() + 3, 32)));
  std::array<std::uint8_t, 3> read{2, write[1], write[2]};
  std::array<std::uint8_t, 33> data{};
  pad.communicate(read, data);
  equal(data[0], 3);
  equal(data[31], 34);
  equal(data[32], response[0]);
  read[2] ^= 1;
  pad.communicate(read, data);
  equal(data[0], 0);
  equal(data[32], 255);
  read[2] ^= 1;
  std::array<std::uint8_t, 4> bank{3, 0x80, 1, 1};
  pad.communicate(bank, response);
  pad.communicate(read, data);
  equal(data[0], 0);
  pad.communicate(write, response);
  equal(pad.pak_data()[0x9000], 3);
  bank[3] = 0;
  pad.communicate(bank, response);
  equal(pad.pak_data()[0x1000], 3);
  pad.rumble_pak();
  pad.communicate(command, status);
  equal(status[2], 3);
  read = {2, 0x80, 1};
  pad.communicate(read, data);
  equal(data[0], 0x80);
  bank = {3, 0xc0, 27, 1};
  pad.communicate(bank, response);
  equal(pad.rumbling(), true);
  pad.disconnect_pak();
  equal(pad.rumbling(), false);
  command[0] = 1;
  std::array<std::uint8_t, 5> input{};
  equal(pad.communicate(command, input).overflow, true);
  equal(input[0], 0);
  equal(input[1], 0xb0);
}

} // namespace test
