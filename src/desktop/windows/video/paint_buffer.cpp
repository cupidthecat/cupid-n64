#include "desktop/windows/video/paint_buffer.hpp"
#include <stdexcept>

namespace cupid::desktop {

PaintBuffer::~PaintBuffer() {
  if (memory_) {
    if (original_)
      SelectObject(memory_, original_);
    if (bitmap_)
      DeleteObject(bitmap_);
    DeleteDC(memory_);
  }
}

HDC PaintBuffer::begin(HDC target, int width, int height) {
  if (width <= 0 || height <= 0)
    throw std::runtime_error("The paint buffer requires a positive size.");
  if (!memory_)
    memory_ = CreateCompatibleDC(target);
  if (!memory_)
    throw std::runtime_error("Could not create the paint buffer.");
  if (width_ != width || height_ != height) {
    const auto next = CreateCompatibleBitmap(target, width, height);
    if (!next)
      throw std::runtime_error("Could not resize the paint buffer.");
    const auto previous = SelectObject(memory_, next);
    if (!previous || previous == HGDI_ERROR) {
      DeleteObject(next);
      throw std::runtime_error("Could not select the paint buffer.");
    }
    if (bitmap_)
      DeleteObject(bitmap_);
    else
      original_ = previous;
    bitmap_ = next;
    width_ = width;
    height_ = height;
  }
  return memory_;
}

void PaintBuffer::present(HDC target) const {
  if (!memory_ || !BitBlt(target, 0, 0, width_, height_, memory_, 0, 0, SRCCOPY))
    throw std::runtime_error("Could not present the video frame.");
}

} // namespace cupid::desktop
