#pragma once

#include "core/cartridge/flash/flash.hpp"
#include "core/devices/cic/cic.hpp"
#include "core/timing/frequencies.hpp"

namespace cupid::n64 {

enum class ControllerAccessory { None, Memory, Rumble, Transfer };

struct CartridgeProfile {
  unsigned byte_swap = 0;
  VideoRegion region = VideoRegion::Ntsc;
  CicModel cic = CicModel::N6102;
  unsigned eeprom_size = 0;
  unsigned sram_size = 0;
  std::optional<FlashModel> flash_model;
  bool rtc_present = false;
  std::array<ControllerAccessory, 4> accessories{};
};

std::optional<CartridgeProfile> inspect_cartridge(std::span<const std::uint8_t> image);

} // namespace cupid::n64
