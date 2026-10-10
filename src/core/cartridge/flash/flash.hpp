#pragma once

#include "core/devices/pi/device.hpp"
#include "core/timing/event_queue.hpp"
#include <array>
#include <optional>
#include <vector>

namespace cupid::n64 {

enum class FlashModel {
  Mx29l0000,
  Mx29l0001,
  Mx29l1100,
  Mx29l1101A,
  Mx29l1101B,
  Mx29l1101C,
  Mn63f81mpn
};

class FlashRam : public PeripheralDevice {
public:
  FlashRam(EventQueue &events, std::optional<FlashModel> model = {});
  void power();
  void complete();
  bool select(std::uint32_t address, PeripheralTiming timing) override;
  std::optional<std::uint16_t> read_half(PeripheralTiming timing) override;
  void write_half(std::uint16_t value, PeripheralTiming timing) override;
  std::span<std::uint8_t> data() {
    return data_;
  }

private:
  friend class CoreState;
  struct Model {
    std::uint16_t manufacturer, device;
    bool word_indexed;
    std::uint32_t sector_clocks, chip_clocks, program_clocks;
  };
  enum class Mode { Array, Status, Silicon, Page };
  enum class Erase { None, Chip, Sector };
  enum class Busy { None, Erase, Program };
  static const std::array<Model, 7> models_;
  bool macronix() const {
    return model_.manufacturer == 0xc2;
  }
  void command(std::uint32_t value);
  void start(Busy operation, std::uint32_t clocks);
  EventQueue &events_;
  const Model &model_;
  std::vector<std::uint8_t> data_;
  std::array<std::uint8_t, 128> page_{};
  Mode mode_ = Mode::Array;
  Erase erase_ = Erase::None;
  Busy busy_ = Busy::None;
  std::uint8_t status_ = 0, sector_ = 0;
  std::uint32_t offset_ = 0;
  std::uint16_t command_high_ = 0, stale_value_ = 0, burst_ = 0;
  std::uint8_t pending_command_ = 0, pending_count_ = 0;
  bool command_valid_ = false, open_bus_ = false, stale_ = false;
};

} // namespace cupid::n64
