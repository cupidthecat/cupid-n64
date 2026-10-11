#pragma once

#include "desktop/controls/shortcuts/bindings.hpp"
#include <optional>
#include <windows.h>

namespace cupid::desktop {

std::optional<ShortcutBindings> choose_shortcuts(HWND owner, ShortcutBindings bindings);

} // namespace cupid::desktop
