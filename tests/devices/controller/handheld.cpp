#include "core/controller/accessories/cartridge/handheld.hpp"
#include "../fixture.hpp"
#include "core/controller/accessories/cartridge/identity.hpp"
#include "core/controller/gamepad.hpp"
#include <algorithm>
#include <string_view>

namespace test {
using namespace cupid::n64;
namespace {

using Board = HandheldCartridge::Board;

std::vector<std::uint8_t> banked_rom(unsigned banks = 256) {
  std::vector<std::uint8_t> rom(banks * 0x4000);
  for (unsigned bank = 0; bank < banks; ++bank)
    std::fill(rom.begin() + bank * 0x4000, rom.begin() + (bank + 1) * 0x4000,
              static_cast<std::uint8_t>(bank));
  return rom;
}

void select(HandheldCartridge &cartridge, unsigned reg, std::uint8_t value) {
  cartridge.write(0xa001, static_cast<std::uint8_t>(reg));
  cartridge.write(0xa000, value);
}

void serial(HandheldCartridge &cartridge, unsigned command, unsigned address,
            std::optional<unsigned> data = {}) {
  cartridge.write(0xa080, 0);
  const unsigned header = 0x400 | (command << 8) | address;
  auto bit = [&](unsigned value) {
    cartridge.write(0xa080, static_cast<std::uint8_t>(0x80 | (value << 1)));
    cartridge.write(0xa080, static_cast<std::uint8_t>(0xc0 | (value << 1)));
  };
  for (unsigned index = 11; index; --index)
    bit((header >> (index - 1)) & 1);
  if (data)
    for (unsigned index = 16; index; --index)
      bit((*data >> (index - 1)) & 1);
}

} // namespace

void handheld_tests() {
  {
    const auto hash = [](std::string_view text) {
      return cartridge_identity::sha256(
          {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
    };
    equal(hash("") == std::array<std::uint32_t, 8>{0xe3b0c442, 0x98fc1c14, 0x9afbf4c8, 0x996fb924,
                                                   0x27ae41e4, 0x649b934c, 0xa495991b, 0x7852b855},
          true);
    equal(hash("abc") == std::array<std::uint32_t, 8>{0xba7816bf, 0x8f01cfea, 0x414140de,
                                                      0x5dae2223, 0xb00361a3, 0x96177a9c,
                                                      0xb410ff61, 0xf20015ad},
          true);
    equal(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              std::array<std::uint32_t, 8>{0x248d6a61, 0xd20638b8, 0xe5c02693, 0x0c3e6039,
                                           0xa33ce459, 0x64ff2167, 0xf6ecedd4, 0x19db06c1},
          true);
  }
  const auto rom = banked_rom();
  {
    HandheldCartridge cartridge;
    equal(cartridge.present(), false);
    equal(cartridge.read(0), 255);
    equal(cartridge.load(std::span(rom).first(100)), false);
    equal(cartridge.load(rom, {Board::Mbc1, 32768}), true);
    equal(cartridge.present(), true);
    equal(cartridge.read(0x4000), 1);
    cartridge.write(0x2000, 0);
    equal(cartridge.read(0x4000), 1);
    cartridge.write(0x2000, 7);
    equal(cartridge.read(0x4000), 7);
    cartridge.write(0x4000, 2);
    equal(cartridge.read(0x4000), 71);
    equal(cartridge.read(0), 0);
    cartridge.write(0x6000, 1);
    equal(cartridge.read(0), 64);
    cartridge.write(0xa000, 0x42);
    equal(cartridge.read(0xa000), 255);
    cartridge.write(0, 10);
    cartridge.write(0xa000, 0x42);
    equal(cartridge.save_ram()[0x4000], 0x42);
    cartridge.power();
    equal(cartridge.read(0x4000), 1);
    equal(cartridge.read(0xa000), 255);
    cartridge.write(0, 10);
    cartridge.write(0x4000, 2);
    cartridge.write(0x6000, 1);
    equal(cartridge.read(0xa000), 0x42);
    equal(cartridge.read(0x8000), 0);
    cartridge.disconnect();
    equal(cartridge.present(), false);
    equal(cartridge.read(0xa000), 255);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mbc1Multicart, 8192}), true);
    cartridge.write(0x2000, 0);
    equal(cartridge.read(0x4000), 0);
    cartridge.write(0x4000, 3);
    equal(cartridge.read(0x4000), 48);
    cartridge.write(0x6000, 1);
    equal(cartridge.read(0), 48);
    cartridge.write(0xa000, 0x6b);
    equal(cartridge.read(0xa000), 0x6b);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mbc2, 256}), true);
    cartridge.write(0, 10);
    cartridge.write(0x2100, 5);
    equal(cartridge.read(0x4000), 5);
    cartridge.write(0xa000, 0x3a);
    cartridge.write(0xa001, 0x7b);
    equal(cartridge.read(0xa000), 0xfa);
    equal(cartridge.read(0xa001), 0xfb);
    equal(cartridge.save_ram()[0], 0xba);
    equal(cartridge.read(0xa200), 0xfa);
    cartridge.write(0x2000, 0);
    equal(cartridge.read(0xa000), 255);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mbc3, 32768}), true);
    cartridge.write(0x2000, 0x80);
    equal(cartridge.read(0x4000), 1);
    cartridge.write(0, 10);
    cartridge.write(0x4000, 8);
    cartridge.write(0xa000, 61);
    equal(cartridge.read(0xa000), 0);
    cartridge.write(0x6000, 1);
    equal(cartridge.read(0xa000), 61);
    cartridge.write(0xa000, 19);
    cartridge.power();
    cartridge.write(0, 10);
    cartridge.write(0x4000, 8);
    equal(cartridge.read(0xa000), 61);
    cartridge.write(0x6000, 0);
    cartridge.write(0x6000, 1);
    equal(cartridge.read(0xa000), 19);
    equal(cartridge.load(rom, {Board::Mbc30, 65536}), true);
    cartridge.write(0x2000, 0x80);
    equal(cartridge.read(0x4000), 0x80);
    cartridge.write(0, 10);
    cartridge.write(0x4000, 7);
    cartridge.write(0xa000, 0x38);
    equal(cartridge.save_ram()[0xe000], 0x38);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mbc5, 131072}), true);
    cartridge.write(0x2000, 0);
    equal(cartridge.read(0x4000), 0);
    cartridge.write(0x2000, 25);
    cartridge.write(0x3000, 1);
    equal(cartridge.read(0x4000), 25);
    cartridge.write(0, 10);
    cartridge.write(0x4000, 8);
    equal(cartridge.rumbling(), true);
    cartridge.write(0xa000, 0x47);
    equal(cartridge.save_ram()[0x10000], 0x47);
    cartridge.write(0x4000, 0);
    equal(cartridge.rumbling(), false);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mbc6, 32768}), true);
    cartridge.write(0x2000, 10);
    cartridge.write(0x3000, 14);
    equal(cartridge.read(0x4000), 5);
    equal(cartridge.read(0x6000), 7);
    cartridge.flash()[10 * 8192] = 0x73;
    cartridge.write(0x2800, 8);
    equal(cartridge.read(0x4000), 0x73);
    cartridge.write(0x4000, 0);
    equal(cartridge.flash()[10 * 8192], 0x73);
    cartridge.write(0, 10);
    cartridge.write(0x0400, 3);
    cartridge.write(0x0800, 5);
    cartridge.write(0xa000, 0x35);
    cartridge.write(0xb000, 0x69);
    equal(cartridge.save_ram()[3 * 4096], 0x35);
    equal(cartridge.save_ram()[5 * 4096], 0x69);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mbc7}), true);
    equal(cartridge.read(0xa020), 255);
    cartridge.write(0, 10);
    cartridge.write(0x4000, 0x40);
    cartridge.motion(256, -256);
    cartridge.write(0xa010, 0xaa);
    equal(cartridge.read(0xa020), 0xcf);
    equal(cartridge.read(0xa030), 0x81);
    equal(cartridge.read(0xa040), 0xd1);
    cartridge.write(0xa000, 0x55);
    equal(cartridge.read(0xa020), 0xd0);
    serial(cartridge, 0, 0xc0);
    serial(cartridge, 1, 7, 0x1235);
    equal(cartridge.eeprom()[14], 0x12);
    equal(cartridge.eeprom()[15], 0x35);
    equal(cartridge.read(0xa080) & 1, 0);
    serial(cartridge, 2, 7);
    equal(cartridge.read(0xa080) & 1, 0);
    unsigned value = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
      cartridge.write(0xa080, 0x80);
      cartridge.write(0xa080, 0xc0);
      value = (value << 1) | (cartridge.read(0xa080) & 1);
    }
    equal(value, 0x1235);
    serial(cartridge, 0, 0);
    serial(cartridge, 1, 7, 0xffff);
    equal(cartridge.eeprom()[14], 0x12);
    equal(cartridge.read(0xb000), 0);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Mmm01, 32768}), true);
    equal(cartridge.read(0), 254);
    equal(cartridge.read(0x4000), 255);
    cartridge.write(0x2000, 32);
    cartridge.write(0, 0);
    equal(cartridge.read(0), 32);
    equal(cartridge.read(0x4000), 33);
    cartridge.write(0x2000, 3);
    equal(cartridge.read(0x4000), 35);
    cartridge.power();
    equal(cartridge.read(0), 254);
  }
  for (const auto board : {Board::Linear, Board::Huc1, Board::Huc3}) {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {board, board == Board::Linear ? 8192u : 32768u}), true);
    if (board == Board::Huc3)
      equal(cartridge.read(0xa000), 1);
    cartridge.write(0, 10);
    cartridge.write(0xa000, 0x45);
    equal(cartridge.read(0xa000), 0x45);
    cartridge.write(0, 0);
    equal(cartridge.read(0xa000), board == Board::Huc3 ? 1 : 0x45);
  }
  {
    HandheldCartridge cartridge;
    equal(cartridge.load(rom, {Board::Tama, 32}), true);
    equal(cartridge.read(0x4000), 0);
    select(cartridge, 0, 7);
    equal(cartridge.read(0x4000), 7);
    select(cartridge, 4, 0xa);
    select(cartridge, 5, 0xb);
    select(cartridge, 6, 0);
    select(cartridge, 7, 3);
    equal(cartridge.save_ram()[3], 0xba);
    select(cartridge, 6, 2);
    select(cartridge, 7, 3);
    cartridge.write(0xa001, 12);
    equal(cartridge.read(0xa000), 0xfa);
    cartridge.write(0xa001, 13);
    equal(cartridge.read(0xa000), 0xfb);
    cartridge.write(0xa001, 10);
    equal(cartridge.read(0xa000), 0xf1);
    equal(cartridge.read(0xa001), 255);
  }
  {
    std::int64_t now = 1000001;
    HandheldCartridge cartridge([&] { return now; });
    equal(cartridge.load(rom, {Board::Mbc3, 32768, true}), true);
    std::array<std::uint8_t, 13> clock{59, 59, 23, 255, 1};
    const std::uint64_t saved = 1000000;
    for (unsigned byte = 0; byte < 8; ++byte)
      clock[5 + byte] = static_cast<std::uint8_t>(saved >> (byte * 8));
    equal(cartridge.load_clock(clock), true);
    const auto current = cartridge.save_clock();
    equal(current[0], 0);
    equal(current[1], 0);
    equal(current[2], 0);
    equal(current[3], 0);
    equal(current[4], 0x80);
    now += 3600;
    equal(cartridge.save_clock()[2], 0);
    equal(cartridge.load_clock(current), true);
    equal(cartridge.save_clock()[2], 1);
    equal(cartridge.load_clock(std::span(clock).first(12)), false);
  }
  {
    const std::int64_t saved = 1000000;
    std::int64_t now = saved + 86400;
    HandheldCartridge cartridge([&] { return now; });
    equal(cartridge.load(rom, {Board::Tama, 32, true}), true);
    std::array<std::uint8_t, 15> clock{0x24, 7, 0x30, 0x11, 0x59, 0x59, 8};
    for (unsigned byte = 0; byte < 8; ++byte)
      clock[7 + byte] = static_cast<std::uint8_t>(std::uint64_t(saved) >> (byte * 8));
    equal(cartridge.load_clock(clock), true);
    auto current = cartridge.save_clock();
    equal(current[1], 8);
    equal(current[2], 1);
    equal(current[3], 0x11);
    now = saved + 1;
    equal(cartridge.load_clock(clock), true);
    current = cartridge.save_clock();
    equal(current[1], 8);
    equal(current[2], 1);
    equal(current[3], 0x11);
    equal(current[4], 0);
    equal(current[5], 0);
    clock[1] = 0x12;
    clock[2] = 0x31;
    clock[3] = 0x23;
    now = saved + 3600;
    equal(cartridge.load_clock(clock), true);
    current = cartridge.save_clock();
    equal(current[0], 0x25);
    equal(current[1], 1);
    equal(current[2], 1);
    equal(current[3], 0);
    equal(current[6], 10);
  }
  {
    auto data = banked_rom(4);
    data[0x147] = 0x22;
    data[0x143] = 0x80;
    std::fill(data.begin() + 0x134, data.begin() + 0x143, 0);
    std::copy_n("CMASTER", 7, data.begin() + 0x134);
    std::copy_n("KCEJ", 4, data.begin() + 0x13f);
    HandheldCartridge cartridge;
    equal(cartridge.load(data), true);
    equal(cartridge.eeprom().size(), 512);
    data[0x13c] = 'X';
    equal(cartridge.load(data), true);
    equal(cartridge.eeprom().size(), 256);
  }
  {
    auto data = banked_rom(72);
    data[0x147] = 3;
    data[0x149] = 3;
    auto cartridge = std::make_shared<HandheldCartridge>();
    equal(cartridge->load(data), true);
    equal(cartridge->save_ram().size(), 32768);
    cartridge->write(0x2000, 17);
    cartridge->write(0x4000, 2);
    equal(cartridge->read(0x4000), 65);
    RandomGenerator random;
    Gamepad pad(random);
    pad.transfer_pak(cartridge);
    std::array<std::uint8_t, 3> identity{};
    const std::array<std::uint8_t, 1> identify{0};
    pad.communicate(identify, identity);
    const auto write = [&](unsigned address, std::uint8_t value) {
      std::array<std::uint8_t, 4> command{
          3, static_cast<std::uint8_t>(address >> 8),
          static_cast<std::uint8_t>((address & 0xe0) |
                                    address_crc(static_cast<std::uint16_t>(address))),
          value};
      std::array<std::uint8_t, 1> response{};
      equal(pad.communicate(command, response).valid, true);
    };
    write(0x8000, 0x84);
    write(0xb000, 1);
    write(0xa000, 1);
    const std::array<std::uint8_t, 3> read{2, 0xc0, 27};
    std::array<std::uint8_t, 33> response{};
    equal(pad.communicate(read, response).valid, true);
    equal(response[0], 1);
    equal(response[32], data_crc(std::span<const std::uint8_t, 32>(response.data(), 32)));
  }
}

} // namespace test
