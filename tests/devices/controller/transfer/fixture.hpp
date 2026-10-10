#pragma once
#include "../../system/reset/fixture.hpp"
#include "board_scenarios.hpp"
#include "core/controller/accessories/cartridge/handheld.hpp"
namespace test::transfer_fixture {
using namespace cupid::n64;
struct WireProbe : reset_fixture::Machine {
  unsigned device = 0;
  unsigned response_status = 0;
  std::array<std::uint8_t, 63> response{};
  bool connected = false;
  void connect(unsigned choice) {
    device = choice;
    connected = true;
    if (choice == 4) {
      console->connect_mouse(0);
      console->mouse(0).input(false, false, 0, 0);
    } else {
      console->connect_controller(0, true);
      auto &pad = console->controller(0);
      pad.input(0, 0, 0);
      if (choice == 1) {
        pad.memory_pak(1);
        for (unsigned n = 0; n < pad.pak_data().size(); ++n)
          pad.pak_data()[n] = peripheral_wire::pattern(n);
      }
      if (choice == 2)
        pad.rumble_pak();
      if (choice == 3)
        pad.transfer_pak();
    }
  }
  void disconnect() {
    if (device == 4) {
      console->connect_mouse(0, false);
      connected = false;
    } else {
      console->controller(0).disconnect_pak();
    }
  }
  unsigned address_crc(unsigned address) {
    return cupid::n64::address_crc(address);
  }
  void command(std::span<const std::uint8_t> data, unsigned receive) {
    response.fill(0xa5);
    const auto status =
        !connected ? cupid::n64::JoybusStatus{}
        : device == 4
            ? console->mouse(0).communicate(data, std::span(response).first(receive))
            : console->controller(0).communicate(data, std::span(response).first(receive));
    response_status = unsigned(status.valid) | (unsigned(status.overflow) << 1);
  }
  void observe() {
    word(response_status);
    for (auto value : response)
      word(value);
    word(device != 4 && console->controller(0).rumbling());
  }
};
struct TransferProbe : WireProbe {
  std::shared_ptr<cupid::n64::HandheldCartridge> cartridge;
  void connect_cart(unsigned board) {
    device = 3;
    connected = true;
    console->connect_controller(0, true);
    cartridge =
        std::make_shared<cupid::n64::HandheldCartridge>([] { return std::int64_t(1700000000); });
    cupid::n64::HandheldCartridge::Config config;
    using Board = cupid::n64::HandheldCartridge::Board;
    constexpr std::array<Board, transfer_board::Boards> boards{
        Board::Linear, Board::Mbc1,          Board::Mbc3,  Board::Mbc5, Board::Mbc2,
        Board::Mbc30,  Board::Mbc1Multicart, Board::Mmm01, Board::Huc1, Board::Huc3,
        Board::Mbc6,   Board::Mbc7,          Board::Tama};
    config.board = boards[board];
    config.ram_size = transfer_board::ram_size(board);
    if (!cartridge->load(transfer_board::rom(board), config))
      throw std::runtime_error("Transfer cartridge load failed");
    for (unsigned n = 0; n < cartridge->save_ram().size(); ++n)
      cartridge->save_ram()[n] = peripheral_wire::pattern(n);
    console->controller(0).transfer_pak(cartridge);
  }
  void detach_cart() {
    cartridge->disconnect();
  }
  void ram_snapshot() {
    word(cartridge->save_ram().size());
    for (auto value : cartridge->save_ram())
      word(value);
  }
  void eeprom_snapshot() {
    word(cartridge->eeprom().size());
    for (auto value : cartridge->eeprom())
      word(value);
  }
};
} // namespace test::transfer_fixture
