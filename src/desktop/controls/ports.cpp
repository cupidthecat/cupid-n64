#include "desktop/controls/ports.hpp"
#include <stdexcept>

namespace cupid::desktop {

void validate_ports(const ControllerPorts &ports) {
  for (const auto &port : ports) {
    if (port.controller < ControllerType::Automatic || port.controller > ControllerType::GameCube ||
        port.accessory < AccessoryType::Automatic || port.accessory > AccessoryType::BioSensor)
      throw std::runtime_error("Select a supported controller and accessory.");
    if (port.heart_rate < 30 || port.heart_rate > 180)
      throw std::runtime_error("Bio Sensor heart rate must be from 30 through 180 BPM.");
  }
}

std::array<PortStatus, 4>
default_ports(const std::array<n64::ControllerAccessory, 4> &accessories) {
  constexpr std::array types{AccessoryType::None, AccessoryType::Memory, AccessoryType::Rumble,
                             AccessoryType::Transfer};
  std::array<PortStatus, 4> defaults;
  for (unsigned port = 0; port < defaults.size(); ++port) {
    defaults[port].accessory = types.at(static_cast<unsigned>(accessories[port]));
    defaults[port].controller = port == 0 || defaults[port].accessory != AccessoryType::None
                                    ? ControllerType::Nintendo64
                                    : ControllerType::Disconnected;
  }
  return defaults;
}

std::array<PortStatus, 4> resolve_ports(const ControllerPorts &settings,
                                        const std::array<PortStatus, 4> &defaults) {
  auto ports = defaults;
  for (unsigned port = 0; port < ports.size(); ++port) {
    const auto &selected = settings[port];
    auto &status = ports[port];
    if (selected.accessory != AccessoryType::Automatic)
      status.accessory = selected.accessory;
    if (selected.controller != ControllerType::Automatic)
      status.controller = selected.controller;
    else if (status.accessory != AccessoryType::None)
      status.controller = ControllerType::Nintendo64;
    status.heart_rate = selected.heart_rate;
    if (status.controller != ControllerType::Nintendo64)
      status.accessory = AccessoryType::None;
    status.cartridge =
        status.accessory == AccessoryType::Transfer ? selected.cartridge : std::filesystem::path{};
  }
  return ports;
}

} // namespace cupid::desktop
