#pragma once

#include "core/controller/accessories/bio_sensor.hpp"
#include "core/controller/accessories/transfer_pak.hpp"
#include "core/devices/joybus/device.hpp"
#include "core/timing/random.hpp"
#include <vector>

namespace cupid::n64 {

class Gamepad : public JoybusDevice {
public:
  explicit Gamepad(RandomGenerator &random);
  // Coordinates are calibrated N64 stick values.
  void input(std::uint16_t buttons, std::int8_t x, std::int8_t y);
  // Host axes use signed 16-bit coordinates with positive Y pointing down.
  void input_host(std::uint16_t buttons, std::int16_t x, std::int16_t y);
  void memory_pak(unsigned banks = 1);
  void rumble_pak();
  void transfer_pak(std::shared_ptr<TransferCartridge> cartridge = {});
  void bio_sensor(BioSensor::HostClock clock = {});
  void disconnect_pak();
  std::uint32_t state() const;
  bool rumbling() const {
    return motor_;
  }
  std::span<std::uint8_t> pak_data() {
    return ram_;
  }
  TransferPak &transfer() {
    return transfer_;
  }
  BioSensor &sensor() {
    return sensor_;
  }
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  friend class CoreState;
  enum class Pak { None, Memory, Rumble, Transfer, BioSensor };
  void format();
  RandomGenerator &random_;
  std::vector<std::uint8_t> ram_;
  TransferPak transfer_;
  BioSensor sensor_;
  Pak pak_ = Pak::None;
  unsigned bank_ = 0;
  std::uint16_t buttons_ = 0;
  std::int8_t x_ = 0;
  std::int8_t y_ = 0;
  bool detect_ = false;
  bool motor_ = false;
};

} // namespace cupid::n64
