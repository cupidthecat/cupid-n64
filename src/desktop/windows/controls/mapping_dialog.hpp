#pragma once

#include "desktop/windows/controls/preferences.hpp"
#include <functional>
#include <optional>
#include <windows.h>

namespace cupid::desktop {

struct MappingForm {
  PlayerMappings mappings;
  std::array<PortStatus, 4> controllers;
  bool available = false;
  std::function<std::array<HostGamepad, 4>()> poll;
};

std::optional<PlayerMappings> choose_mappings(HWND owner, MappingForm form);

} // namespace cupid::desktop
