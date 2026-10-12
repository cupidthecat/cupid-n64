#include "desktop/session.hpp"

namespace cupid::desktop {

std::vector<n64::MemoryRegion> Session::memory_regions() const {
  return n64::Inspection::regions(*console_);
}

std::vector<std::uint8_t> Session::read_memory(n64::MemoryRegionId region, std::uint32_t offset,
                                               unsigned length) {
  output_->synchronize();
  return n64::Inspection::read(*console_, region, offset, length);
}

void Session::write_memory(n64::MemoryRegionId region, std::uint32_t offset,
                           std::span<const std::uint8_t> bytes) {
  output_->synchronize();
  n64::Inspection::write(*console_, region, offset, bytes);
}

std::vector<n64::DeviceProperty> Session::device_properties(n64::DeviceId device) const {
  return n64::Inspection::properties(*console_, device);
}

} // namespace cupid::desktop
