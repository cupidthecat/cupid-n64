#include "core/devices/pif/pif.hpp"

namespace cupid::n64 {

void Pif::access(bool read, bool wide) {
  if (read && wide) {
    if (ram_[63] & 2)
      challenge();
    else
      joy_run();
  } else if (!read && (ram_[63] & 1)) {
    ram_[63] &= ~1u;
    joy_init();
    joy_parse();
  }
}

void Pif::joy_init() {
  joy_skip_.fill(true);
  joy_reset_.fill(false);
}

void Pif::joy_parse() {
  unsigned offset = 0;
  unsigned channel = 0;
  while (channel < 5 && offset < 64) {
    auto send = unsigned(ram_[(offset++) & 63]);
    if (send == 0xfe)
      break;
    if (send == 0xff)
      continue;
    if (!send) {
      ++channel;
      continue;
    }
    if (send == 0xfd) {
      joy_reset_[channel++] = true;
      continue;
    }
    const auto start = offset - 1;
    const auto receive = ram_[(offset++) & 63] & 63;
    send &= 63;
    offset += send + receive;
    if (offset < 64) {
      joy_address_[channel] = static_cast<std::uint8_t>(start);
      joy_skip_[channel++] = false;
    }
  }
}

void Pif::joy_run() {
  for (int channel = 4; channel >= 0; --channel) {
    if (joy_reset_[channel]) {
      if (channel < 4 && devices_[channel])
        devices_[channel]->reset();
      continue;
    }
    if (joy_skip_[channel])
      continue;
    unsigned offset = joy_address_[channel];
    auto send = unsigned(ram_[(offset++) & 63]);
    if (send & 128)
      continue;
    if (send & 64) {
      if (channel < 4 && devices_[channel])
        devices_[channel]->reset();
      continue;
    }
    const auto receive_offset = offset;
    const auto receive = ram_[offset & 63] & 63;
    send &= 63;
    ram_[(offset++) & 63] = static_cast<std::uint8_t>(receive);
    std::array<std::uint8_t, 64> input{}, output{};
    for (unsigned n = 0; n < send; ++n)
      input[n] = ram_[(offset++) & 63];
    JoybusStatus status;
    if (devices_[channel]) {
      const auto command_size = channel == 4 && !send ? 1u : send;
      status = devices_[channel]->communicate(std::span(input).first(command_size),
                                              std::span(output).first(receive));
    }
    if (!status.valid)
      ram_[receive_offset & 63] = static_cast<std::uint8_t>(128 | receive);
    if (status.overflow)
      ram_[receive_offset & 63] = static_cast<std::uint8_t>(64 | receive);
    if (status.valid) {
      for (unsigned n = 0; n < static_cast<unsigned>(receive); ++n)
        ram_[(offset++) & 63] = output[n];
    }
  }
}

std::uint32_t Pif::estimate_timing() const {
  unsigned clocks = 13600;
  unsigned short_commands = 0;
  unsigned offset = 0;
  unsigned channel = 0;
  while (offset < 64 && channel < 5) {
    const auto send = ram_[(offset++) & 63];
    if (send == 0xfe) {
      ++short_commands;
      break;
    }
    if (send == 0xff || send == 0xfd || send == 0) {
      ++short_commands;
      if (send != 0xff)
        ++channel;
      continue;
    }
    const auto receive = ram_[(offset++) & 63];
    offset += (send & 63) + (receive & 63);
    clocks += channel < 4 ? (devices_[channel] ? 22000 : 18000) : 20000;
    ++channel;
  }
  return clocks + short_commands * 1420;
}

void Pif::challenge() {
  cic_.write_bit(true);
  cic_.write_bit(false);
  cic_.read_nibble();
  cic_.read_nibble();
  for (unsigned n = 0; n < 15; ++n) {
    const auto value = ram_[0x30 + n];
    cic_.write_nibble(value >> 4);
    cic_.write_nibble(value & 15);
  }
  cic_.read_bit();
  for (unsigned n = 0; n < 15; ++n) {
    const auto upper = cic_.read_nibble() << 4;
    ram_[0x30 + n] = static_cast<std::uint8_t>(upper | cic_.read_nibble());
  }
}

} // namespace cupid::n64
