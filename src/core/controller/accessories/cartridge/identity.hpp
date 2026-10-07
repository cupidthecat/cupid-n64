#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace cupid::n64::cartridge_identity {

std::array<std::uint32_t, 8> sha256(std::span<const std::uint8_t> data);
bool multicart(std::span<const std::uint8_t> rom);

} // namespace cupid::n64::cartridge_identity
