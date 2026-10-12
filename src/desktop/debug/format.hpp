#pragma once

#include "core/debug/inspection.hpp"

namespace cupid::desktop {

std::uint64_t inspection_address(std::string_view text);
std::uint32_t inspection_offset(const n64::MemoryRegion &region, std::uint64_t address);
std::vector<std::uint8_t> inspection_bytes(std::string_view text);
std::string inspection_dump(std::uint64_t address, std::span<const std::uint8_t> bytes);
std::string inspection_properties(std::span<const n64::DeviceProperty> properties);

} // namespace cupid::desktop
