#pragma once

#include "core/devices/mi/mips_interface.hpp"
#include "core/timing/frequencies.hpp"
#include <functional>

namespace cupid::n64 {

struct StereoSample {
  double left = 0;
  double right = 0;
};

struct AudioState {
  std::array<std::uint32_t, 2> addresses{};
  std::array<std::uint32_t, 2> lengths{};
  unsigned count = 0;
  bool enable = false;
  bool carry = false;
  std::uint16_t dac_rate = 0;
  std::uint8_t bit_rate = 0;
};

class AudioInterface {
public:
  AudioInterface(Rdram &ram, MipsInterface &interrupts, VideoRegion region = VideoRegion::Ntsc);
  void power();
  void advance(std::uint32_t clocks);
  void elapse(std::uint32_t clocks) {
    clock_ -= clocks;
  }
  void run();
  StereoSample sample();
  std::uint32_t read_word(std::uint32_t address) const;
  void write_word(std::uint32_t address, std::uint32_t value);
  void connect(std::function<void(StereoSample)> sample, std::function<void(unsigned)> rate = {});
  void connect_sync(std::function<void()> callback);
  const AudioState &state() const {
    return state_;
  }
  StereoSample output() const {
    return output_;
  }
  unsigned frequency() const {
    return frequency_;
  }
  unsigned precision() const {
    return precision_;
  }
  unsigned period() const {
    return period_;
  }
  std::int64_t clocks() const {
    return clock_;
  }

private:
  void update_decay();
  Rdram &ram_;
  MipsInterface &interrupts_;
  VideoRegion region_;
  AudioState state_{};
  StereoSample output_{};
  std::int64_t clock_ = 0;
  unsigned frequency_ = 44100;
  unsigned precision_ = 16;
  unsigned period_ = 0;
  double decay_ = 0;
  std::function<void(StereoSample)> sample_;
  std::function<void(unsigned)> rate_;
  std::function<void()> sync_;
};

} // namespace cupid::n64
