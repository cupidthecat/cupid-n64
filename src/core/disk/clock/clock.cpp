#include "clock.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>

namespace cupid::n64 {
namespace {

unsigned decode(unsigned value) {
  return (value >> 4) * 10 + (value & 15);
}
std::uint8_t encode(unsigned value) {
  return static_cast<std::uint8_t>((value / 10) * 16 + value % 10);
}
unsigned month_days(unsigned year, unsigned month) {
  return month == 2 ? 28 + unsigned(year % 4 == 0) : 30 + ((month + month / 8) & 1);
}

} // namespace

DiskClock::DiskClock(HostClock clock) : clock_(std::move(clock)) {
  if (!clock_)
    clock_ = [] {
      return std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
          .count();
    };
  data_.fill(255);
  initialize();
}

void DiskClock::initialize() {
  const auto value = static_cast<std::time_t>(clock_());
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &value);
#else
  localtime_r(&value, &local);
#endif
  data_[0] = encode(static_cast<unsigned>(local.tm_year % 100));
  data_[1] = encode(static_cast<unsigned>(local.tm_mon + 1));
  data_[2] = encode(static_cast<unsigned>(local.tm_mday));
  data_[3] = encode(static_cast<unsigned>(local.tm_hour));
  data_[4] = encode(static_cast<unsigned>(local.tm_min));
  data_[5] = encode(static_cast<unsigned>(local.tm_sec));
}

bool DiskClock::valid() const {
  for (unsigned field = 0; field < 6; ++field)
    if ((data_[field] & 15) >= 10)
      return false;
  if (data_[0] >= 0xa0 || data_[1] < 1 || data_[1] > 0x12 || data_[2] < 1 || data_[3] >= 0x24 ||
      data_[4] >= 0x60 || data_[5] >= 0x60)
    return false;
  return decode(data_[2]) <= month_days(decode(data_[0]), decode(data_[1]));
}

bool DiskClock::load(std::span<const std::uint8_t> data) {
  if (data.size() != data_.size())
    return false;
  std::copy(data.begin(), data.end(), data_.begin());
  const auto erased = [](auto bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 255; });
  };
  if (erased(data.first(8)) || erased(data.last(8))) {
    initialize();
    return true;
  }
  if (!valid()) {
    std::fill_n(data_.begin(), 8, 255);
    return true;
  }
  std::uint64_t timestamp = 0;
  for (unsigned byte = 0; byte < 8; ++byte)
    timestamp |= std::uint64_t(data_[8 + byte]) << (byte * 8);
  const auto now = clock_();
  if (now > static_cast<std::int64_t>(timestamp))
    advance(static_cast<std::uint64_t>(now) - timestamp);
  return true;
}

std::array<std::uint8_t, 16> DiskClock::save() const {
  auto data = data_;
  const auto timestamp = static_cast<std::uint64_t>(clock_());
  for (unsigned byte = 0; byte < 8; ++byte)
    data[8 + byte] = static_cast<std::uint8_t>(timestamp >> (byte * 8));
  return data;
}

std::uint16_t DiskClock::read(unsigned pair) const {
  const auto offset = (pair * 2) & 14;
  return static_cast<std::uint16_t>((unsigned(data_[offset]) << 8) | data_[offset + 1]);
}

void DiskClock::write(unsigned pair, std::uint16_t value) {
  const auto offset = (pair * 2) & 14;
  data_[offset] = static_cast<std::uint8_t>(value >> 8);
  data_[offset + 1] = static_cast<std::uint8_t>(value);
}

void DiskClock::tick() {
  if (valid())
    advance(1);
}

void DiskClock::advance(std::uint64_t elapsed) {
  auto year = decode(data_[0]), month = decode(data_[1]), day = decode(data_[2]);
  const auto seconds = decode(data_[3]) * 3600 + decode(data_[4]) * 60 + decode(data_[5]);
  const auto remainder = elapsed % 86400 + seconds;
  data_[3] = encode(static_cast<unsigned>((remainder % 86400) / 3600));
  data_[4] = encode(static_cast<unsigned>((remainder % 3600) / 60));
  data_[5] = encode(static_cast<unsigned>(remainder % 60));
  unsigned ordinal = year * 365 + (year + 3) / 4 + day - 1;
  for (unsigned previous = 1; previous < month; ++previous)
    ordinal += month_days(year, previous);
  ordinal = static_cast<unsigned>((ordinal + elapsed / 86400 + remainder / 86400) % 36525);
  year = 0;
  while (ordinal >= 365 + unsigned(year % 4 == 0)) {
    ordinal -= 365 + unsigned(year % 4 == 0);
    ++year;
  }
  month = 1;
  while (ordinal >= month_days(year, month)) {
    ordinal -= month_days(year, month);
    ++month;
  }
  data_[0] = encode(year);
  data_[1] = encode(month);
  data_[2] = encode(ordinal + 1);
}

} // namespace cupid::n64
