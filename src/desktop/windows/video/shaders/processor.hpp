#pragma once

#include "desktop/windows/video/shaders/selection.hpp"
#include "renderer/video/frame.hpp"
#include <memory>
#include <span>
#include <string>

namespace cupid::desktop {

class ShaderProcessor {
public:
  ShaderProcessor();
  ~ShaderProcessor();
  void select(ShaderSelection selection);
  std::span<const std::uint8_t> apply(const n64::VideoFrame &frame);
  const ShaderSelection &selection() const;
  const std::string &error() const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace cupid::desktop
