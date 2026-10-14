#pragma once

namespace cupid::desktop {

struct PlaybackSync {
  bool audio = true;
  bool video = false;
  bool operator==(const PlaybackSync &) const = default;
};
enum class FocusBehavior { Pause, BlockInput, AllowInput };

} // namespace cupid::desktop
