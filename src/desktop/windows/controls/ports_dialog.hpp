#pragma once

#include "desktop/controls/ports.hpp"
#include <optional>
#include <string_view>
#include <windows.h>

namespace cupid::desktop {

inline constexpr std::array<std::wstring_view, 5> controller_names{
    L"Automatic", L"Disconnected", L"Nintendo 64 gamepad", L"N64 mouse", L"GameCube controller"};
inline constexpr std::array<std::wstring_view, 6> accessory_names{
    L"Automatic", L"Empty slot", L"Controller Pak", L"Rumble Pak", L"Transfer Pak", L"Bio Sensor"};

struct PortForm {
  ControllerPorts settings;
  std::array<PortStatus, 4> current, defaults;
  std::filesystem::path directory;
};

std::optional<ControllerPorts> choose_ports(HWND owner, PortForm form);

} // namespace cupid::desktop
