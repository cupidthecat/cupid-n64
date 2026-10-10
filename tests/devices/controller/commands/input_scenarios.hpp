#pragma once
#include "wire_scenarios.hpp"
#include <array>
#include <span>

namespace test::peripheral_input {
constexpr std::array<int, 13> axes{-32768, -32767, -16384, -4096, -1,    0,   1,
                                   4096,   16384,  32766,  32767, -8192, 8192};
template <class Probe> void scenarios(Probe &p) {
  p.reset(false);
  p.connect_devices();
  for (unsigned device = 0; device < 2; ++device)
    for (unsigned buttons :
         {0u, 0x8000u, 0x4000u, 0x2000u, 0x1000u, 0x0f00u, 0x0300u, 0x0c00u, 0x1030u, 0xffffu})
      for (unsigned x = 0; x < axes.size(); ++x)
        for (unsigned y = 0; y < axes.size(); ++y)
          for (unsigned receive : {1u, 3u, 4u, 5u}) {

            p.input(device, buttons, axes[x], axes[y]);
            p.command(device, receive);
            p.observe();
            if (x == axes.size() - 1 && y == axes.size() - 1 && receive == 5)
              p.finish();
          }
}
} // namespace test::peripheral_input
