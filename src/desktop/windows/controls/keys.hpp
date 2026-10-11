#pragma once

#include "desktop/controls/shortcuts/bindings.hpp"
#include <string>

namespace cupid::desktop {

std::wstring key_name(unsigned code);
std::wstring shortcut_text(ShortcutBinding binding);
std::wstring shortcut_text(const std::array<ShortcutBinding, 2> &bindings);

} // namespace cupid::desktop
