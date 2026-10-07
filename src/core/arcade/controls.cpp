#include "core/arcade/aleck64.hpp"
#include <algorithm>

namespace cupid::n64 {

std::uint8_t Aleck64::mahjong_state() const {
  constexpr std::array<std::array<int, 6>, 4> keys{{{-1, 1, 5, 9, 13, 17},
                                                    {-2, 0, 4, 8, 12, 14},
                                                    {-1, 2, 6, 10, 16, 18},
                                                    {-1, 3, 7, 11, 15, -1}}};
  unsigned value = 255;
  for (unsigned row = 0; row < keys.size(); ++row) {
    if (!(mahjong_row_ & (1u << row)))
      continue;
    for (unsigned bit = 0; bit < keys[row].size(); ++bit) {
      const auto key = keys[row][bit];
      if ((key == -2 && input_.players[0].start) || (key >= 0 && (input_.mahjong & (1u << key))))
        value &= ~(1u << bit);
    }
  }
  return static_cast<std::uint8_t>(value);
}

std::uint32_t Aleck64::read_port(unsigned port) const {
  std::uint32_t value = 0xffffffff;
  const bool e90 = profile_ == ArcadeProfile::MagicalTetris;
  auto bit = [&](unsigned index, bool pressed) {
    if (pressed)
      value &= ~(1u << index);
  };
  if (port == 0) {
    for (unsigned n = 0; n < 2; ++n) {
      const auto &player = input_.players[n];
      const auto offset = n * 8;
      bit(offset, player.up);
      bit(offset + 1, player.down);
      bit(offset + 2, player.left);
      bit(offset + 3, player.right);
      for (unsigned button = 0; button < (e90 ? 2u : 4u); ++button)
        bit(offset + 4 + button, player.buttons[button]);
      if (e90)
        bit(offset + 7, player.start);
    }
    return (value & 0xffff) | (unsigned(dip_switches_[1]) << 16) |
           (unsigned(dip_switches_[0]) << 24);
  }
  if (port == 1) {
    if (e90) {
      bit(0, input_.players[0].coin);
      bit(1, input_.players[1].coin);
      bit(4, input_.service);
      bit(5, input_.test);
    } else {
      bit(16, input_.players[0].start);
      bit(17, input_.players[1].start);
      bit(18, input_.players[0].coin);
      bit(19, input_.players[1].coin);
      bit(20, input_.service);
      bit(21, input_.test);
    }
  }
  if (port == 2 &&
      (profile_ == ArcadeProfile::HiPai || profile_ == ArcadeProfile::SuperRealMahjong))
    return (value & 0xff00ffff) | (unsigned(mahjong_state()) << 16);
  return value;
}

std::uint32_t Aleck64::controller_state(unsigned port) const {
  const auto &player = input_.players[port];
  const bool disabled = profile_ == ArcadeProfile::ElevenBeat;
  const unsigned buttons =
      (unsigned(player.buttons[0]) << 15) | (unsigned(player.buttons[1]) << 14) |
      (unsigned(player.start) << 12) | (unsigned(player.up || disabled) << 11) |
      (unsigned(player.down || disabled) << 10) | (unsigned(player.left || disabled) << 9) |
      (unsigned(player.right || disabled) << 8) | (unsigned(player.buttons[2]) << 4) |
      unsigned(player.buttons[3]);
  return (buttons << 16) | (unsigned(static_cast<std::uint8_t>(player.x)) << 8) |
         unsigned(static_cast<std::uint8_t>(player.y));
}

JoybusStatus Aleck64::Controller::communicate(std::span<const std::uint8_t> input,
                                              std::span<std::uint8_t> output) {
  if (input.empty())
    return {};
  if (input[0] == 0 || input[0] == 255) {
    constexpr std::array<std::uint8_t, 3> status{5, 0, 2};
    std::copy_n(status.begin(), std::min(output.size(), status.size()), output.begin());
    return {true, false};
  }
  if (input[0] == 1) {
    const auto state = board_.controller_state(player_);
    for (unsigned n = 0; n < std::min(output.size(), std::size_t(4)); ++n)
      output[n] = static_cast<std::uint8_t>(state >> ((3 - n) * 8));
    return {true, output.size() > 4};
  }
  return {};
}

} // namespace cupid::n64
