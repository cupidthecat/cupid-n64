#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace cupid::n64 {

struct PeripheralTiming {
  unsigned latency = 0;
  unsigned pulse_width = 0;
  unsigned release = 0;

  bool permits(const PeripheralTiming &minimum) const {
    return latency >= minimum.latency && pulse_width >= minimum.pulse_width &&
           release >= minimum.release;
  }
};

class PeripheralDevice {
public:
  virtual ~PeripheralDevice() = default;
  virtual bool select(std::uint32_t address, PeripheralTiming timing) = 0;
  virtual std::optional<std::uint16_t> read_half(PeripheralTiming timing) = 0;
  virtual void write_half(std::uint16_t value, PeripheralTiming timing) = 0;
};

class PeripheralMemory : public PeripheralDevice {
public:
  std::optional<std::uint16_t> read_half(PeripheralTiming) override {
    if (offset_ >= view_.size())
      return {};
    const auto value =
        static_cast<std::uint16_t>((unsigned(view_[offset_]) << 8) | view_[offset_ + 1]);
    offset_ += 2;
    return value;
  }

  void write_half(std::uint16_t value, PeripheralTiming) override {
    if (offset_ >= view_.size())
      return;
    if (writable_) {
      view_[offset_] = static_cast<std::uint8_t>(value >> 8);
      view_[offset_ + 1] = static_cast<std::uint8_t>(value);
    }
    offset_ += 2;
  }

protected:
  friend class CoreState;
  std::span<std::uint8_t> view_;
  std::uint32_t offset_ = 0;
  bool writable_ = false;
  PeripheralTiming minimum_;
};

} // namespace cupid::n64
