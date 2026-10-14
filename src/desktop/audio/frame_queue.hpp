#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <vector>

namespace cupid::desktop {

class AudioFrameQueue {
public:
  explicit AudioFrameQueue(std::size_t capacity = 65536) : frames_(capacity) {
    if (!capacity)
      throw std::invalid_argument("An audio queue needs space for samples.");
  }
  std::size_t size() const {
    return count_;
  }
  void clear() {
    begin_ = count_ = 0;
  }
  void discard(std::size_t frames) {
    frames = std::min(frames, count_);
    begin_ = (begin_ + frames) % frames_.size();
    count_ -= frames;
  }
  void append(std::span<const float> samples) {
    if (samples.size() % 2)
      throw std::invalid_argument("Audio samples must contain stereo pairs.");
    for (std::size_t n = 0; n < samples.size(); n += 2) {
      if (count_ == frames_.size())
        discard(1);
      frames_[(begin_ + count_) % frames_.size()] = {samples[n], samples[n + 1]};
      ++count_;
    }
  }
  void read(std::span<float> output) {
    if (output.size() % 2 || output.size() / 2 > count_)
      throw std::invalid_argument("The audio queue does not contain the requested samples.");
    for (std::size_t n = 0; n < output.size(); n += 2) {
      const auto &frame = frames_[(begin_ + n / 2) % frames_.size()];
      output[n] = frame[0];
      output[n + 1] = frame[1];
    }
    discard(output.size() / 2);
  }

private:
  std::vector<std::array<float, 2>> frames_;
  std::size_t begin_ = 0, count_ = 0;
};

} // namespace cupid::desktop
