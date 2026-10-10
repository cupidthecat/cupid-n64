#pragma once

#include "core/devices/pi/device.hpp"
#include <cstddef>
#include <vector>

namespace cupid::n64 {

class PeripheralInterface;

class IsViewer : public PeripheralDevice {
public:
  explicit IsViewer(PeripheralInterface &pi) : pi_(pi) {}
  void connect(std::size_t cartridge_size);
  void power();
  bool select(std::uint32_t address, PeripheralTiming timing) override;
  std::optional<std::uint16_t> read_half(PeripheralTiming timing) override;
  void write_half(std::uint16_t value, PeripheralTiming timing) override;

private:
  friend class CoreState;
  PeripheralInterface &pi_;
  std::vector<std::uint8_t> ram_;
  std::uint32_t offset_ = 0;
};

} // namespace cupid::n64
