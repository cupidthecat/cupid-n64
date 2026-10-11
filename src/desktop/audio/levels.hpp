#pragma once

#include <cstdint>
#include <span>

namespace cupid::desktop {

struct AudioLevels {
  unsigned volume = 100;
  int balance = 0;
  bool operator==(const AudioLevels &) const = default;
};

void render_audio(std::span<const std::int16_t> samples, std::span<std::int16_t> output,
                  AudioLevels levels);

} // namespace cupid::desktop
