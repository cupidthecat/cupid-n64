#pragma once
#include "../commands/wire_scenarios.hpp"
#include <vector>

namespace test::transfer_board {
constexpr unsigned Phases = 40;
constexpr unsigned Boards = 13;
constexpr unsigned ram_size(unsigned board) {
  return board == 4 ? 256 : board == 12 ? 32 : board == 5 ? 65536 : 32768;
}
inline std::vector<std::uint8_t> rom(unsigned board) {
  std::vector<std::uint8_t> data(0x200000);
  for (unsigned n = 0; n < data.size(); ++n)
    data[n] = static_cast<std::uint8_t>((n >> 14) * 17 + (n & 0x3fff) * 7 + (n >> 7) + 0x31);
  constexpr std::array<std::uint8_t, Boards> types{8,    3,    0x13, 0x1b, 6,    0x13, 3,
                                                   0x0d, 0xff, 0xfe, 0x20, 0x22, 0xfd};
  data[0x147] = types[board];
  data[0x148] = 6;
  data[0x149] = 3;
  return data;
}
template <class Probe> void write(Probe &p, unsigned address, unsigned value) {
  std::array<std::uint8_t, 4> command{
      3, static_cast<std::uint8_t>(address >> 8),
      static_cast<std::uint8_t>((address & 0xe0) | p.address_crc(address)),
      static_cast<std::uint8_t>(value)};
  p.command(command, 1);
  p.observe();
}
template <class Probe> void read(Probe &p, unsigned address) {
  test::peripheral_wire::command(p, 2, address, 3, 33, true);
}
template <class Probe> void cart_write(Probe &p, unsigned address, unsigned value) {
  write(p, 0xa000, address >> 14);
  write(p, 0xc000 | (address & 0x3fe0), value);
}
template <class Probe> void cart_read(Probe &p, unsigned address) {
  write(p, 0xa000, address >> 14);
  read(p, 0xc000 | (address & 0x3fe0));
}
template <class Probe> void targeted(Probe &p, unsigned board, unsigned bank, unsigned window) {
  write(p, 0xb000, 0);
  write(p, 0xb000, 1);
  read(p, 0xb000);
  cart_write(p, 0x0000, 0x0a);
  cart_write(p, 0x2100, bank);
  cart_write(p, 0x0400, window);
  cart_write(p, 0x0800, (window + 1) & 7);
  cart_write(p, 0x0000, 0x0a);
  cart_write(p, 0x2800, 8);
  cart_read(p, 0x4000);
  cart_write(p, 0x2800, 0);
  cart_write(p, 0x3000, 1);
  cart_write(p, 0x3800, 8);
  cart_read(p, 0x6000);
  cart_write(p, 0x3800, 0);
  cart_write(p, 0x4000, board == 11 ? 0x40 : window);
  cart_write(p, 0x0000, 0x0a);
  cart_write(p, 0xa000, 0x5a);
  cart_read(p, 0xa000);
  cart_read(p, 0xa020);
  cart_write(p, 0xb000, 0xa5);
  cart_read(p, 0xb000);
  p.ram_snapshot();
}
template <class Probe> void scenarios(Probe &p) {
  for (unsigned board = 0; board < Boards; ++board)
    for (unsigned cart_bank : {0u, 1u, 31u, 127u})
      for (unsigned window : {0u, 1u, 2u, 3u}) {
        p.reset(false);
        p.connect_cart(board);

        test::peripheral_wire::command(p, 0, 0, 1, 3, true);
        read(p, 0x8000);
        write(p, 0x8000, 0x84);
        read(p, 0x8000);
        read(p, 0xa000);
        read(p, 0xb000);
        write(p, 0xb000, 1);
        read(p, 0xb000);
        read(p, 0xb000);
        write(p, 0xa000, 0);
        write(p, 0xc000, 0x0a);
        write(p, 0xe000, cart_bank);
        write(p, 0xa000, 1);
        write(p, 0xc000, window);
        write(p, 0xe000, 1);
        read(p, 0xc000);
        read(p, 0xffe0);
        write(p, 0xa000, 2);
        read(p, 0xc000);
        read(p, 0xe000);
        write(p, 0xa000, 2);
        write(p, 0xe000, 0x5a);
        read(p, 0xe000);
        p.ram_snapshot();
        write(p, 0xb000, 0);
        read(p, 0xb000);
        read(p, 0xb000);
        read(p, 0xb000);
        write(p, 0xb000, 1);
        read(p, 0xb000);
        read(p, 0xe000);
        write(p, 0x8000, 0xfe);
        read(p, 0x8000);
        write(p, 0x8000, 0x84);
        read(p, 0xa000);
        read(p, 0xb000);
        p.detach_cart();
        read(p, 0xb000);
        write(p, 0xb000, 1);
        read(p, 0xb000);
        read(p, 0xc000);
        p.power(true);
        read(p, 0xb000);
        p.detach_cart();
        p.connect_cart(board);
        test::peripheral_wire::command(p, 0, 0, 1, 3, true);
        write(p, 0x8000, 0x84);
        targeted(p, board, cart_bank, window);
        p.finish();
      }
}
} // namespace test::transfer_board
