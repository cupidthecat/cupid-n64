#pragma once

#include "desktop/windows/settings/transfer.hpp"
#include <optional>
#include <windows.h>

namespace cupid::desktop {

std::optional<SettingsTransfer> review_settings_paths(HWND owner, SettingsTransfer settings);

} // namespace cupid::desktop
