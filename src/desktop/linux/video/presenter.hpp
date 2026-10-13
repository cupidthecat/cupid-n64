#pragma once

#include "desktop/video/presentation_worker.hpp"
#include "renderer/video/frame.hpp"
#include <memory>
#include <span>

namespace cupid::desktop::linux_host {

class VideoPresenter {
public:
  VideoPresenter();
  ~VideoPresenter();
  VideoPresenter(const VideoPresenter &) = delete;
  VideoPresenter &operator=(const VideoPresenter &) = delete;
  bool initialize(std::uintptr_t parent);
  void prepare(const VideoSurface &surface);
  void show(bool visible);
  bool render(std::span<const std::uint8_t> pixels, unsigned width, unsigned height,
              const VideoSurface &surface);
  std::vector<std::uint8_t> capture();
  void release_graphics();
  void close();
  bool available() const;
  const std::string &error() const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::desktop::linux_host
