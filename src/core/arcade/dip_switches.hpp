#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace cupid::n64 {

enum class ArcadeProfile : std::uint8_t;

struct DipValue {
  std::string_view label;
  std::optional<std::uint8_t> bits;
};

struct DipSetting {
  std::string_view name, default_value;
  unsigned bank;
  std::uint8_t mask;
  std::span<const DipValue> choices;
  bool boolean = false;
  std::string_view initial_latch{};
};

std::span<const DipSetting> arcade_dip_settings(ArcadeProfile profile);

} // namespace cupid::n64
