#pragma once

#include "core/devices/joybus/device.hpp"
#include "core/memory/bus.hpp"
#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace cupid::n64 {

enum class ArcadeProfile : std::uint8_t {
  Disabled,
  Standard,
  ElevenBeat,
  StarSoldier,
  HiPai,
  SuperRealMahjong,
  MagicalTetris,
};

std::optional<ArcadeProfile> arcade_profile(std::string_view name);

class Aleck64 {
public:
  struct Player {
    bool up = false, down = false, left = false, right = false;
    std::array<bool, 9> buttons{};
    bool start = false, coin = false;
    std::int8_t x = 0, y = 0;
  };
  struct Input {
    std::array<Player, 2> players;
    bool service = false, test = false;
    std::uint32_t mahjong = 0;
  };
  explicit Aleck64(ArcadeProfile profile);
  Aleck64(const Aleck64 &) = delete;
  Aleck64 &operator=(const Aleck64 &) = delete;
  void power(bool reset = false);
  BusRead read(std::uint32_t address, unsigned bytes) const;
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value);
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words) const;
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> words);
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const;
  Input &input() {
    return input_;
  }
  std::array<std::uint8_t, 2> &dip_switches() {
    return dip_switches_;
  }
  JoybusDevice &controller(unsigned port) {
    return controllers_.at(port);
  }

private:
  friend class CoreState;
  class Controller : public JoybusDevice {
  public:
    Controller(Aleck64 &board, unsigned player) : board_(board), player_(player) {}
    JoybusStatus communicate(std::span<const std::uint8_t> input,
                             std::span<std::uint8_t> output) override;

  private:
    Aleck64 &board_;
    unsigned player_;
  };
  std::uint32_t read_port(unsigned port) const;
  std::uint8_t mahjong_state() const;
  std::uint32_t controller_state(unsigned player) const;
  ArcadeProfile profile_;
  std::vector<std::uint32_t> sdram_;
  std::array<std::uint32_t, 1024> video_ram_{}, palette_ram_{};
  Input input_{};
  std::array<std::uint8_t, 2> dip_switches_{};
  std::array<Controller, 2> controllers_{{{*this, 0}, {*this, 1}}};
  std::uint8_t mahjong_row_ = 0;
  bool video_enabled_ = false;
};

} // namespace cupid::n64
