#include "core/controller/accessories/bio_sensor.hpp"
#include <algorithm>
#include <chrono>

namespace cupid::n64 {

void BioSensor::connect(HostClock clock) {
  clock_ = std::move(clock);
  if (!clock_)
    clock_ = [] {
      return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count());
    };
  bpm_ = 60;
  next_ = clock_();
}

void BioSensor::disconnect() {
  next_ = start_ = 0;
  pulsing_ = false;
  clock_ = {};
}

void BioSensor::beats_per_minute(unsigned value) {
  bpm_ = std::clamp(value, 30u, 180u);
}

void BioSensor::update() {
  if (!clock_)
    return;
  const auto now = clock_();
  if (pulsing_ && now - start_ >= 200000)
    pulsing_ = false;
  if (!pulsing_ && now >= next_) {
    pulsing_ = true;
    start_ = now;
    next_ = now + 60000000 / bpm_;
  }
}

std::uint8_t BioSensor::read(std::uint16_t address) const {
  if (address >= 0xc000)
    return pulsing_ ? 0 : 3;
  return address >= 0x8000 ? 0x81 : 0;
}

} // namespace cupid::n64
