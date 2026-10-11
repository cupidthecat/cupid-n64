#pragma once

namespace cupid::desktop {

enum class DisplayScale : unsigned { Fit, Integer, Stretch };
enum class DisplayAspect : unsigned { Standard, Pixels };

struct Viewport {
  unsigned x = 0, y = 0, width = 0, height = 0;
};

Viewport image_viewport(unsigned image_width, unsigned image_height, unsigned area_width,
                        unsigned area_height, DisplayScale scale, DisplayAspect aspect);

} // namespace cupid::desktop
