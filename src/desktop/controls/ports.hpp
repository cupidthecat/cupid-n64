#pragma once

#include "core/cartridge/profile/profile.hpp"
#include <array>
#include <filesystem>

namespace cupid::desktop {

enum class ControllerType { Automatic, Disconnected, Nintendo64, Mouse, GameCube };
enum class AccessoryType { Automatic, None, Memory, Rumble, Transfer, BioSensor };

struct PortSettings {
  ControllerType controller = ControllerType::Automatic;
  AccessoryType accessory = AccessoryType::Automatic;
  std::filesystem::path cartridge;
  unsigned heart_rate = 60;
  bool operator==(const PortSettings &) const = default;
};

struct PortStatus {
  ControllerType controller = ControllerType::Disconnected;
  AccessoryType accessory = AccessoryType::None;
  std::filesystem::path cartridge;
  unsigned heart_rate = 60;
};

using ControllerPorts = std::array<PortSettings, 4>;

void validate_ports(const ControllerPorts &ports);
std::array<PortStatus, 4> default_ports(const std::array<n64::ControllerAccessory, 4> &accessories);
std::array<PortStatus, 4> resolve_ports(const ControllerPorts &settings,
                                        const std::array<PortStatus, 4> &defaults);

} // namespace cupid::desktop
