#pragma once

#include "core/devices/cic/cic.hpp"
#include <optional>

namespace cupid::n64::cartridge {

struct BootChip {
  CicModel model;
  unsigned byte_swap;
};

std::uint64_t boot_checksum(std::span<const std::uint8_t> boot, unsigned seed,
                            unsigned byte_swap = 0);
std::optional<BootChip> identify_boot(std::span<const std::uint8_t> boot);

} // namespace cupid::n64::cartridge
