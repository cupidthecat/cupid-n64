#pragma once

#include <optional>
#include <windows.h>

namespace cupid::desktop {

struct ViewportSize {
  unsigned width = 960, height = 720;
};

std::optional<ViewportSize> choose_viewport_size(HWND owner, ViewportSize current);

} // namespace cupid::desktop
