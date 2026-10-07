#include "core/cartridge/flash/flash.hpp"

namespace cupid::n64 {

const std::array<FlashRam::Model, 7> FlashRam::models_{{
    {0xc2, 0x00, true, 15937500, 15937500, 656250},
    {0xc2, 0x01, true, 15937500, 15937500, 656250},
    {0xc2, 0x1e, true, 15937500, 15937500, 656250},
    {0xc2, 0x1d, false, 15937500, 15937500, 656250},
    {0xc2, 0x84, false, 15937500, 15937500, 656250},
    {0xc2, 0x8e, false, 15937500, 15937500, 656250},
    {0x32, 0xf1, false, 52500000, 56250000, 56250},
}};

FlashRam::FlashRam(EventQueue &events, std::optional<FlashModel> model)
    : events_(events), model_(models_[model && static_cast<unsigned>(*model) < models_.size()
                                          ? static_cast<unsigned>(*model)
                                          : 3]),
      data_(model ? 131072 : 0, 255) {
  power();
}

void FlashRam::power() {
  events_.cancel(Event::FlashComplete);
  mode_ = Mode::Array;
  status_ = macronix() ? 0x8c : 0x80;
  erase_ = Erase::None;
  sector_ = 0;
  busy_ = Busy::None;
  page_.fill(255);
  offset_ = 0;
  command_high_ = stale_value_ = burst_ = 0;
  pending_command_ = pending_count_ = 0;
  command_valid_ = open_bus_ = stale_ = false;
}

void FlashRam::start(Busy operation, std::uint32_t clocks) {
  mode_ = Mode::Status;
  status_ = static_cast<std::uint8_t>((status_ & ~0x80) | (operation == Busy::Erase ? 2 : 1));
  stale_ = macronix();
  busy_ = operation;
  events_.insert(Event::FlashComplete, clocks);
}

void FlashRam::complete() {
  status_ = static_cast<std::uint8_t>((status_ | 0x80) & ~3);
  if (!macronix()) {
    if (busy_ == Busy::Erase)
      status_ |= 8;
    if (busy_ == Busy::Program)
      status_ |= 4;
  }
  busy_ = Busy::None;
}

} // namespace cupid::n64
