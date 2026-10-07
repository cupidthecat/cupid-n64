#include "core/cartridge/rtc/rtc.hpp"

namespace cupid::n64 {

void Rtc::advance(std::uint64_t elapsed) {
  const auto decode = [](std::uint8_t value) {
    return static_cast<std::uint8_t>((value >> 4) * 10 + (value & 15));
  };
  const auto encode = [](unsigned value) {
    value &= 255;
    return static_cast<std::uint8_t>((value / 10 << 4) | (value % 10));
  };
  auto seconds = decode(data_[16]);
  auto minutes = decode(data_[17]);
  auto hours = decode(data_[18] & 0x7f);
  auto day = decode(data_[19]);
  auto weekday = decode(data_[20]);
  auto month = decode(data_[21]);
  auto year = decode(data_[22]) + 100 * decode(data_[23]);
  while (elapsed--) {
    if (++seconds != 60)
      continue;
    seconds = 0;
    if (++minutes != 60)
      continue;
    minutes = 0;
    if (++hours != 24)
      continue;
    hours = 0;
    if (++weekday == 7)
      weekday = 0;
    const auto days = month == 2 ? (year % 4 == 0 ? 29u : 28u) : 30 + ((month + (month >> 3)) & 1);
    if (++day > days) {
      day = 1;
      if (++month == 13) {
        month = 1;
        ++year;
      }
    }
  }
  data_[16] = encode(seconds);
  data_[17] = encode(minutes);
  data_[18] = encode(hours) | 0x80;
  data_[19] = encode(day);
  data_[20] = encode(weekday);
  data_[21] = encode(month);
  data_[22] = encode(year % 100);
  data_[23] = encode(year / 100);
}

} // namespace cupid::n64
