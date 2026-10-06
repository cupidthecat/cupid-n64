#include "core/devices/audio/audio_interface.hpp"
#include <algorithm>

namespace cupid::n64 {

std::uint32_t AudioInterface::read_word(std::uint32_t address) const {
  if (((address & 31) >> 2) != 3)
    return state_.lengths[0];
  const auto value = unsigned(state_.count > 1) | (1u << 20) | (1u << 24) |
                     (unsigned(state_.enable) << 25) | (unsigned(state_.count > 0) << 30) |
                     (unsigned(state_.count > 1) << 31);
  if (sync_)
    sync_();
  return value;
}

void AudioInterface::write_word(std::uint32_t address, std::uint32_t value) {
  switch ((address & 31) >> 2) {
  case 0:
    if (state_.count < 2)
      state_.addresses[state_.count] = value & 0x00fffff8;
    break;
  case 1:
    if (state_.count < 2) {
      if (state_.count == 0)
        interrupts_.raise(Interrupt::Audio);
      state_.lengths[state_.count++] = value & 0x0003fff8;
    }
    break;
  case 2:
    state_.enable = value & 1;
    break;
  case 3:
    interrupts_.lower(Interrupt::Audio);
    break;
  case 4: {
    const auto previous = frequency_;
    state_.dac_rate = static_cast<std::uint16_t>(value & 0x3fff);
    frequency_ = std::max(1u, video_frequency(region_) / (state_.dac_rate + 1));
    period_ = clock_frequency / frequency_;
    if (previous != frequency_) {
      if (rate_)
        rate_(frequency_);
      update_decay();
    }
    break;
  }
  case 5:
    state_.bit_rate = static_cast<std::uint8_t>(value & 15);
    precision_ = state_.bit_rate + 1;
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
