#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cupid::desktop::gdb {

int digit(char value);
std::optional<std::uint64_t> number(std::string_view text);
std::string hex(std::uint64_t value, unsigned width = 1);
std::string encode(std::span<const std::uint8_t> bytes);
std::optional<std::vector<std::uint8_t>> decode(std::string_view text);
std::string packet(std::string_view payload);
std::optional<std::string> unescape(std::string_view payload, unsigned limit);

} // namespace cupid::desktop::gdb
