#pragma once

#include "cupid/vi.hpp"

#include <optional>
#include <vector>

namespace cupid::host {

inline constexpr unsigned max_video_field_width = 640;
inline constexpr unsigned max_video_field_height = 288;

struct DisplayFrame {
    unsigned width{};
    unsigned height{};
    std::vector<u32> pixels;

    bool operator==(const DisplayFrame&) const = default;
};

struct DisplayRect {
    unsigned x{};
    unsigned y{};
    unsigned width{};
    unsigned height{};

    bool operator==(const DisplayRect&) const = default;
};

class FrameComposer {
  public:
    [[nodiscard]] std::optional<DisplayFrame> compose(VideoField field);
    void reset();

  private:
    std::optional<VideoField> previous_field_;
};

[[nodiscard]] DisplayRect letterbox_4_3(unsigned window_width, unsigned window_height);

} // namespace cupid::host
