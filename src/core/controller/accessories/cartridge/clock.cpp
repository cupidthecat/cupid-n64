#include "implementation.hpp"
#include <algorithm>
#include <ctime>
#include <limits>

namespace cupid::n64 {
namespace {

unsigned decode(unsigned value) {
  return (value >> 4) * 10 + (value & 15);
}
std::uint8_t encode(unsigned value) {
  return static_cast<std::uint8_t>((value / 10) * 16 + value % 10);
}

std::tm local_time(std::int64_t value) {
  const auto timestamp = static_cast<std::time_t>(value);
  std::tm result{};
#ifdef _WIN32
  localtime_s(&result, &timestamp);
#else
  localtime_r(&timestamp, &result);
#endif
  return result;
}

} // namespace

void HandheldCartridge::Implementation::initialize_clock() {
  if (!config.rtc)
    return;
  const auto now = clock();
  const auto local = local_time(now);
  if (config.board == Board::Tama) {
    calendar = {static_cast<std::uint8_t>(local.tm_year % 100),
                static_cast<std::uint8_t>(local.tm_mon + 1),
                static_cast<std::uint8_t>(local.tm_mday),
                static_cast<std::uint8_t>(local.tm_hour),
                static_cast<std::uint8_t>(local.tm_min),
                static_cast<std::uint8_t>(local.tm_sec),
                static_cast<std::uint8_t>((calendar[6] & 0xfe) | unsigned(local.tm_hour >= 12))};
  } else {
    const auto day = static_cast<unsigned>((now / 86400) & 511);
    time = {static_cast<std::uint8_t>(local.tm_sec), static_cast<std::uint8_t>(local.tm_min),
            static_cast<std::uint8_t>(local.tm_hour), static_cast<std::uint8_t>(day),
            static_cast<std::uint8_t>(day >> 8)};
  }
}

bool HandheldCartridge::Implementation::load_clock(std::span<const std::uint8_t> data) {
  const bool tama = config.board == Board::Tama;
  const unsigned size = tama ? 15 : 13;
  if (!config.rtc || data.size() != size)
    return false;
  const unsigned offset = tama ? 7 : 5;
  if (tama) {
    for (unsigned field = 0; field < 6; ++field)
      calendar[field] = static_cast<std::uint8_t>(decode(data[field]));
    calendar[6] = data[6];
  } else {
    constexpr std::array<std::uint8_t, 5> masks{63, 63, 31, 255, 0xc1};
    for (unsigned field = 0; field < 5; ++field)
      time[field] = data[field] & masks[field];
  }
  std::uint64_t timestamp = 0;
  for (unsigned byte = 0; byte < 8; ++byte)
    timestamp |= std::uint64_t(data[offset + byte]) << (byte * 8);
  if (!timestamp || timestamp == std::numeric_limits<std::uint64_t>::max()) {
    initialize_clock();
    return true;
  }
  const auto now = clock();
  if (now <= static_cast<std::int64_t>(timestamp))
    return true;
  auto elapsed = static_cast<std::uint64_t>(now) - timestamp;
  if (!tama) {
    const auto day = [&] {
      const auto next = (((unsigned(time[4]) & 1) << 8) | time[3]) + 1;
      time[3] = static_cast<std::uint8_t>(next);
      time[4] = static_cast<std::uint8_t>((time[4] & 0xc0) | ((next >> 8) & 1));
      if (next == 512)
        time[4] |= 0x80;
    };
    const auto hour = [&] {
      time[2] = (time[2] + 1) & 31;
      if (time[2] == 24) {
        time[2] = 0;
        day();
      }
    };
    const auto minute = [&] {
      time[1] = (time[1] + 1) & 63;
      if (time[1] == 60) {
        time[1] = 0;
        hour();
      }
    };
    const auto days = elapsed / 86400;
    const auto previous = ((unsigned(time[4]) & 1) << 8) | time[3];
    const auto next = previous + days;
    time[3] = static_cast<std::uint8_t>(next);
    time[4] = static_cast<std::uint8_t>((time[4] & 0xc0) | ((next >> 8) & 1));
    if (next >= 512)
      time[4] |= 0x80;
    elapsed %= 86400;
    while (elapsed >= 3600) {
      hour();
      elapsed -= 3600;
    }
    while (elapsed >= 60) {
      minute();
      elapsed -= 60;
    }
    while (elapsed) {
      time[0] = (time[0] + 1) & 63;
      if (time[0] == 60) {
        time[0] = 0;
        minute();
      }
      --elapsed;
    }
  } else {
    const auto day = [&] {
      constexpr std::array<unsigned, 12> lengths{31, 28, 31, 30, 31, 30, 30, 31, 30, 31, 30, 31};
      const auto month = (unsigned(calendar[1]) + 11) % 12;
      const auto length = lengths[month] + unsigned(month == 1 && !(calendar[6] & 6));
      if (++calendar[2] > length) {
        calendar[2] = 1;
        if (++calendar[1] > 12) {
          calendar[1] = 1;
          calendar[6] = static_cast<std::uint8_t>((calendar[6] & ~6u) | ((calendar[6] + 2) & 6));
          if (++calendar[0] >= 100)
            calendar[0] = 0;
        }
      }
    };
    const auto hour = [&] {
      if (!(calendar[6] & 8)) {
        if (++calendar[3] >= 12) {
          calendar[3] = 0;
          calendar[6] ^= 1;
        }
        if (!calendar[3] && !(calendar[6] & 1))
          day();
      } else {
        if (++calendar[3] >= 24) {
          calendar[3] = 0;
          calendar[6] &= 0xfe;
        }
        if (!calendar[3])
          day();
      }
    };
    const auto minute = [&] {
      if (++calendar[4] >= 60) {
        calendar[4] = 0;
        day();
      }
    };
    while (elapsed >= 86400) {
      day();
      elapsed -= 86400;
    }
    while (elapsed >= 3600) {
      hour();
      elapsed -= 3600;
    }
    while (elapsed >= 60) {
      minute();
      elapsed -= 60;
    }
    while (elapsed) {
      if (++calendar[5] >= 60) {
        calendar[5] = 0;
        minute();
      }
      --elapsed;
    }
  }
  return true;
}

std::vector<std::uint8_t> HandheldCartridge::Implementation::save_clock() const {
  if (!config.rtc)
    return {};
  const bool tama = config.board == Board::Tama;
  std::vector<std::uint8_t> data(tama ? 15 : 13);
  const unsigned offset = tama ? 7 : 5;
  if (tama) {
    for (unsigned field = 0; field < 6; ++field)
      data[field] = encode(calendar[field]);
    data[6] = calendar[6];
  } else {
    std::copy(time.begin(), time.end(), data.begin());
  }
  const auto timestamp = static_cast<std::uint64_t>(clock());
  for (unsigned byte = 0; byte < 8; ++byte)
    data[offset + byte] = static_cast<std::uint8_t>(timestamp >> (byte * 8));
  return data;
}

} // namespace cupid::n64
