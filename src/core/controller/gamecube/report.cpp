#include "core/controller/gamecube/gamecube.hpp"

namespace cupid::n64 {

std::array<std::uint8_t, 8> GameCubePad::report(unsigned mode) const {
  std::array<std::uint8_t, 8> value{
      static_cast<std::uint8_t>((buttons_ & 31) | (origin_pending_ ? 32 : 0)),
      static_cast<std::uint8_t>(0x80 | (buttons_ >> 8)),
      analog_[0],
      analog_[1],
      analog_[2],
      analog_[3],
      analog_[4],
      analog_[5]};
  const auto triggers = static_cast<std::uint8_t>((analog_[4] & 0xf0) | (analog_[5] >> 4));
  if (mode == 1 || mode == 2) {
    value[4] = static_cast<std::uint8_t>((analog_[2] & 0xf0) | (analog_[3] >> 4));
    value[5] = mode == 1 ? analog_[4] : triggers;
    value[6] = mode == 1 ? analog_[5] : 0;
    value[7] = 0;
  } else if (mode == 4) {
    value[6] = 0;
    value[7] = 0;
  } else if (mode != 3) {
    value[6] = triggers;
    value[7] = 0;
  }
  return value;
}

} // namespace cupid::n64
