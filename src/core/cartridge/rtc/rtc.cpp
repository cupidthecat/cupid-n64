#include "core/cartridge/rtc/rtc.hpp"
#include <algorithm>
#include <bit>
#include <ctime>

namespace cupid::n64 {

Rtc::Rtc(EventQueue &events, bool present, HostClock clock)
    : events_(events),
      clock_(clock ? std::move(clock) : HostClock([] { return std::time(nullptr); })),
      present_(present) {
  data_.fill(255);
  if (present_)
    restore_time();
}

void Rtc::run(bool enabled) {
  status_ = static_cast<std::uint8_t>((status_ & 0x7f) | (enabled ? 0 : 0x80));
  events_.remove(Event::RtcTick);
  if (enabled && present_)
    events_.insert(Event::RtcTick, 187500000);
}

void Rtc::power() {
  if (present_)
    run(running());
}

void Rtc::tick() {
  if (!running())
    return;
  advance(1);
  run(true);
}

bool Rtc::load(std::span<const std::uint8_t> storage) {
  if (storage.size() != data_.size())
    return false;
  std::copy(storage.begin(), storage.end(), data_.begin());
  present_ = true;
  restore_time();
  run(running());
  return true;
}

Rtc::Storage Rtc::save() {
  if (present_) {
    auto timestamp = static_cast<std::uint64_t>(clock_());
    for (unsigned n = 0; n < 8; ++n)
      data_[24 + n] = static_cast<std::uint8_t>(timestamp >> ((7 - n) * 8));
  }
  return data_;
}

void Rtc::restore_time() {
  std::uint64_t timestamp = 0;
  for (unsigned n = 24; n < 32; ++n)
    timestamp = (timestamp << 8) | data_[n];
  const auto now = clock_();
  if (timestamp != 0xffffffffffffffffull) {
    const auto saved = std::bit_cast<std::int64_t>(timestamp);
    if (now > saved)
      advance(static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(saved));
    return;
  }
  const auto time = static_cast<std::time_t>(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  const auto bcd = [](unsigned value) {
    return static_cast<std::uint8_t>((value / 10 << 4) | (value % 10));
  };
  data_[16] = bcd(local.tm_sec);
  data_[17] = bcd(local.tm_min);
  data_[18] = bcd(local.tm_hour) | 0x80;
  data_[19] = bcd(local.tm_mday);
  data_[20] = bcd(local.tm_wday);
  data_[21] = bcd(local.tm_mon + 1);
  data_[22] = bcd(local.tm_year % 100);
  data_[23] = bcd(local.tm_year / 100);
}

} // namespace cupid::n64
