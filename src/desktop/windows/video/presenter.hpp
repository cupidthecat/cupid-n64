#pragma once

#include "desktop/video/viewport.hpp"
#include "renderer/video/frame.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <windows.h>

namespace cupid::desktop {

class VideoPresenter {
public:
  VideoPresenter();
  ~VideoPresenter();
  bool initialize(HWND parent, bool software_device = false);
  bool present(std::span<const std::uint8_t> pixels, unsigned width, unsigned height,
               unsigned viewport_width, unsigned viewport_height, DisplayScale scale,
               DisplayAspect aspect);
  std::vector<std::uint8_t> capture();
  bool available() const;
  void hide();
  const std::string &error() const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::desktop
