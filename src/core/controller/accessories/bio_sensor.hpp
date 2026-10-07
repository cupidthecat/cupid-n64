#pragma once

#include <cstdint>
#include <functional>

namespace cupid::n64 {

class BioSensor {
public:
  using HostClock = std::function<std::uint64_t()>;
  void connect(HostClock clock = {});
  void disconnect();
  void update();
  void beats_per_minute(unsigned value);
  std::uint8_t read(std::uint16_t address) const;

private:
  HostClock clock_;
  std::uint64_t next_ = 0;
  std::uint64_t start_ = 0;
  unsigned bpm_ = 60;
  bool pulsing_ = false;
};

} // namespace cupid::n64
