#pragma once

#include "core/cartridge/eeprom.hpp"
#include "core/cartridge/rtc/rtc.hpp"

namespace cupid::n64 {

class CartridgeJoybus : public JoybusDevice {
public:
  CartridgeJoybus(Eeprom &eeprom, Rtc &rtc) : eeprom_(eeprom), rtc_(rtc) {}
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  Eeprom &eeprom_;
  Rtc &rtc_;
};

} // namespace cupid::n64
