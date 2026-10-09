#include "core/devices/vi/video_interface.hpp"
#include <algorithm>

namespace cupid::n64 {

const VideoFrame &VideoInterface::scanout(Rdram &ram) {
  auto &frame = software_frame_;
  if (frame.rgba.empty()) {
    frame.width = 640;
    frame.height = region_ == VideoRegion::Pal ? 576 : 480;
    frame.rgba.resize(std::size_t(frame.width) * frame.height * 4);
  }
  const int horizontal_origin = region_ == VideoRegion::Pal ? 128 : 108;
  const int vertical_origin = region_ == VideoRegion::Pal ? 44 : 34;
  const int horizontal_limit = horizontal_origin + static_cast<int>(frame.width);
  const int vertical_limit = vertical_origin + static_cast<int>(frame.height);
  const int horizontal_start = (registers_[9] >> 16) & 1023;
  const int horizontal_end = registers_[9] & 1023;
  const int vertical_start = (registers_[10] >> 16) & 1023;
  const int vertical_end = registers_[10] & 1023;
  const int left = std::max(horizontal_start, horizontal_origin) + 8;
  const int right =
      std::min(horizontal_end, horizontal_limit) - (horizontal_end < horizontal_limit ? 7 : 0);
  const int top = std::max(vertical_start, vertical_origin);
  const int bottom =
      std::min(vertical_end < vertical_start ? vertical_limit : vertical_end, vertical_limit);
  const unsigned depth = registers_[0] & 3;
  const bool interlaced = registers_[0] & 64;
  const auto x_step = registers_[12] & 4095;
  const auto y_step = registers_[13] & 4095;
  const auto x_offset = registers_[12] >> 16;
  const auto y_offset = registers_[13] >> 16;
  const auto bytes = depth == 2 ? 2u : 4u;

  for (unsigned y = 0; y < frame.height; ++y) {
    if (interlaced && (y & 1) == unsigned(field_))
      continue;
    auto *line = frame.rgba.data() + std::size_t(y) * frame.width * 4;
    for (unsigned x = 0; x < frame.width; ++x) {
      line[x * 4 + 0] = line[x * 4 + 1] = line[x * 4 + 2] = 0;
      line[x * 4 + 3] = 255;
    }
    const int scanline = vertical_origin + static_cast<int>(y);
    if (depth < 2 || scanline < top || scanline >= bottom)
      continue;
    const auto source_y = (y_offset + y_step * (scanline - vertical_start)) >> 11;
    const auto row = registers_[1] + source_y * registers_[2] * bytes;
    auto source_x = x_offset + x_step * (left - horizontal_start);
    for (int x = left; x < right; ++x, source_x += x_step) {
      const auto value =
          static_cast<std::uint32_t>(ram.read(row + (source_x >> 10) * bytes, bytes));
      auto *pixel = line + (x - horizontal_origin) * 4;
      if (depth == 2) {
        for (unsigned channel = 0; channel < 3; ++channel) {
          const auto component = (value >> (11 - channel * 5)) & 31;
          pixel[channel] = static_cast<std::uint8_t>((component << 3) | (component >> 2));
        }
      } else {
        pixel[0] = static_cast<std::uint8_t>(value >> 24);
        pixel[1] = static_cast<std::uint8_t>(value >> 16);
        pixel[2] = static_cast<std::uint8_t>(value >> 8);
      }
    }
  }
  return frame;
}

} // namespace cupid::n64
