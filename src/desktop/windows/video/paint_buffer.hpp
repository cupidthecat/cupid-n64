#pragma once

#include <windows.h>

namespace cupid::desktop {

class PaintBuffer {
public:
  PaintBuffer() = default;
  ~PaintBuffer();
  PaintBuffer(const PaintBuffer &) = delete;
  PaintBuffer &operator=(const PaintBuffer &) = delete;
  HDC begin(HDC target, int width, int height);
  void present(HDC target) const;

private:
  HDC memory_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ original_ = nullptr;
  int width_ = 0, height_ = 0;
};

} // namespace cupid::desktop
