#pragma once

#include "core/controller/accessories/transfer_pak.hpp"
#include <functional>
#include <span>
#include <vector>

namespace cupid::n64 {

class HandheldCartridge final : public TransferCartridge {
public:
  enum class Board {
    Auto,
    Linear,
    Mbc1,
    Mbc1Multicart,
    Mbc2,
    Mbc3,
    Mbc30,
    Mbc5,
    Mbc6,
    Mbc7,
    Mmm01,
    Huc1,
    Huc3,
    Tama
  };
  struct Config {
    Board board = Board::Auto;
    unsigned ram_size = 0;
    bool rtc = false;
    unsigned eeprom_size = 256;
  };
  using HostClock = std::function<std::int64_t()>;

  explicit HandheldCartridge(HostClock clock = {});
  ~HandheldCartridge() override;
  bool load(std::span<const std::uint8_t> rom);
  bool load(std::span<const std::uint8_t> rom, Config config);
  void disconnect();
  bool present() const override;
  void power() override;
  std::uint8_t read(std::uint16_t address) override;
  void write(std::uint16_t address, std::uint8_t value) override;
  void motion(std::int16_t x, std::int16_t y);
  bool rumbling() const;
  std::span<std::uint8_t> save_ram();
  std::span<std::uint8_t> flash();
  std::span<std::uint8_t> eeprom();
  bool load_clock(std::span<const std::uint8_t> data);
  std::vector<std::uint8_t> save_clock() const;

private:
  friend class CoreState;
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::n64
