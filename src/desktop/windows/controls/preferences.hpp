#pragma once

#include "desktop/controls/mapping.hpp"
#include "desktop/controls/ports.hpp"
#include <filesystem>
#include <string>

namespace cupid::desktop {

using PlayerMappings = std::array<ControllerMapping, 4>;

std::wstring controller_settings_key(const std::filesystem::path &image);
ControllerPorts read_ports(const std::filesystem::path &settings, const std::wstring &key);
void save_ports(const std::filesystem::path &settings, const std::wstring &key,
                const ControllerPorts &ports);
PlayerMappings read_mappings(const std::filesystem::path &settings);
void save_mappings(const std::filesystem::path &settings, const PlayerMappings &mappings);

} // namespace cupid::desktop
