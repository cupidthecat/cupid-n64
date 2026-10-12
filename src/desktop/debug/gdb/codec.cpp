#include "desktop/debug/gdb/codec.hpp"
#include <charconv>

namespace cupid::desktop::gdb {
namespace {
constexpr std::string_view digits = "0123456789abcdef";
}

int digit(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

std::optional<std::uint64_t> number(std::string_view text) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size())
    return {};
  return value;
}

std::string hex(std::uint64_t value, unsigned width) {
  std::string result;
  do {
    result.insert(result.begin(), digits[value & 15]);
    value >>= 4;
  } while (value || result.size() < width);
  return result;
}

std::string encode(std::span<const std::uint8_t> bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto value : bytes) {
    result += digits[value >> 4];
    result += digits[value & 15];
  }
  return result;
}

std::optional<std::vector<std::uint8_t>> decode(std::string_view text) {
  if (text.size() & 1)
    return {};
  std::vector<std::uint8_t> result;
  result.reserve(text.size() / 2);
  for (std::size_t n = 0; n < text.size(); n += 2) {
    const auto high = digit(text[n]), low = digit(text[n + 1]);
    if (high < 0 || low < 0)
      return {};
    result.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return result;
}

std::string packet(std::string_view payload) {
  std::string result = "$";
  std::uint8_t checksum = 0;
  const auto append = [&](char value) {
    result += value;
    checksum = static_cast<std::uint8_t>(checksum + static_cast<std::uint8_t>(value));
  };
  for (auto value : payload) {
    if (value == '$' || value == '#' || value == '}' || value == '*') {
      append('}');
      value ^= 0x20;
    }
    append(value);
  }
  result += '#';
  result += hex(checksum, 2);
  return result;
}

std::optional<std::string> unescape(std::string_view payload, unsigned limit) {
  std::string result;
  for (std::size_t n = 0; n < payload.size(); ++n) {
    if (payload[n] == '}') {
      if (++n == payload.size())
        return {};
      result += static_cast<char>(payload[n] ^ 0x20);
    } else if (payload[n] == '*') {
      if (result.empty() || ++n == payload.size())
        return {};
      const auto repeats = static_cast<unsigned char>(payload[n]);
      if (repeats < 32 || repeats > 126 || result.size() + repeats - 29 > limit)
        return {};
      result.append(repeats - 29, result.back());
    } else
      result += payload[n];
    if (result.size() > limit)
      return {};
  }
  return result;
}

} // namespace cupid::desktop::gdb
