#pragma once
#include "board_scenarios.hpp"

namespace test::transfer_special {
template <class Probe>
void offset_write(Probe &p, unsigned address, unsigned value, unsigned preceding) {
  test::transfer_board::write(p, 0xa000, address >> 14);
  const auto offset = address & 31;
  const auto pak_address = 0xc000 | (address & 0x3fe0);
  std::array<std::uint8_t, 35> command{};
  command[0] = 3;
  command[1] = static_cast<std::uint8_t>(pak_address >> 8);
  command[2] = static_cast<std::uint8_t>((pak_address & 0xe0) | p.address_crc(pak_address));
  command[3] = static_cast<std::uint8_t>(preceding);
  command[3 + offset] = static_cast<std::uint8_t>(value);
  p.command(std::span(command).first(4 + offset), 1);
  p.observe();
}
template <class Probe>
void select_tama(Probe &p, unsigned index, unsigned value, unsigned preceding) {
  offset_write(p, 0xa001, index, preceding);
  test::transfer_board::cart_write(p, 0xa000, value);
}
template <class Probe> void serial_bits(Probe &p, unsigned value, unsigned bits) {
  for (unsigned n = bits; n > 0; --n) {
    const auto bit = (value >> (n - 1)) & 1;
    test::transfer_board::cart_write(p, 0xa080, 0x80 | (bit << 1));
    test::transfer_board::cart_write(p, 0xa080, 0xc0 | (bit << 1));
    test::transfer_board::cart_read(p, 0xa080);
  }
}
template <class Probe> void eeprom(Probe &p, unsigned address, unsigned value) {
  using namespace test::transfer_board;
  cart_write(p, 0x0000, 10);
  cart_write(p, 0x4000, 0x40);
  cart_write(p, 0xa080, 0);
  serial_bits(p, 0x4c0, 11);
  cart_write(p, 0xa080, 0);
  serial_bits(p, (0x500 | address) << 16 | value, 27);
  cart_read(p, 0xa080);
  p.eeprom_snapshot();
  cart_write(p, 0xa080, 0);
  serial_bits(p, 0x600 | address, 11);
  serial_bits(p, 0, 20);
  p.eeprom_snapshot();
}
template <class Probe> void tama(Probe &p, unsigned address, unsigned value) {
  using namespace test::transfer_board;
  select_tama(p, 10, 0, 0);
  cart_read(p, 0xa000);
  select_tama(p, 0, address, 0);
  select_tama(p, 1, address >> 4, address);
  cart_read(p, 0x4000);
  select_tama(p, 4, value & 15, address >> 4);
  select_tama(p, 5, value >> 4, value & 15);
  select_tama(p, 6, (address >> 4) & 1, value >> 4);
  select_tama(p, 7, address & 15, (address >> 4) & 1);
  p.ram_snapshot();
  select_tama(p, 6, 2 | ((address >> 4) & 1), address & 15);
  select_tama(p, 7, address & 15, 2 | ((address >> 4) & 1));
  offset_write(p, 0xa001, 12, address & 15);
  cart_read(p, 0xa000);
  offset_write(p, 0xa001, 13, 0);
  cart_read(p, 0xa000);
  p.ram_snapshot();
}
template <class Probe> void scenarios(Probe &p) {
  for (unsigned board : {11u, 12u})
    for (unsigned address : {0u, 1u, 31u, 127u})
      for (unsigned value : {0u, 0x1234u, 0xffffu}) {
        p.reset(false);
        p.connect_cart(board);

        test::peripheral_wire::command(p, 0, 0, 1, 3, true);
        test::transfer_board::write(p, 0x8000, 0x84);
        test::transfer_board::write(p, 0xb000, 1);
        test::transfer_board::read(p, 0xb000);
        if (board == 11)
          eeprom(p, address, value);
        else
          tama(p, address, value & 255);
        p.finish();
      }
}
} // namespace test::transfer_special
