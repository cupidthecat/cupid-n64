#include "core/rdp/rdp.hpp"

namespace cupid::n64 {

std::uint32_t Rdp::read_word(std::uint32_t address, std::int64_t caller_clock, bool cpu) const {
  const auto index = (address & 31) >> 2;
  std::uint32_t value = 0;
  switch (index) {
  case 0:
    value = command_.start;
    break;
  case 1:
    value = command_.end;
    break;
  case 2:
    value = command_.current;
    break;
  case 3:
    value = unsigned(command_.source) | (unsigned(command_.freeze || command_.crashed) << 1) |
            (unsigned(command_.flush) << 2) | (unsigned(command_.start_clock) << 3) |
            (unsigned(command_.tmem_busy > 0) << 4) | (unsigned(command_.pipe_busy > 0) << 5) |
            (unsigned(command_.buffer_busy > 0) << 6) | (unsigned(command_.ready) << 7) |
            (unsigned(command_.end_valid) << 9) | (unsigned(command_.start_valid) << 10);
    break;
  case 4:
    value = static_cast<std::uint32_t>(command_.clock - (clock_ - caller_clock) / 3) & 0xffffff;
    break;
  case 5:
    value = command_.buffer_busy;
    break;
  case 6:
    value = command_.pipe_busy;
    break;
  default:
    break;
  }
  if (sync_ && index == 4)
    sync_();
  if (sync_ && cpu && index <= 6)
    sync_();
  if (value == 7) {
    value = command_.tmem_busy;
    if (sync_ && cpu)
      sync_();
  }
  return value;
}

void Rdp::write_word(std::uint32_t address, std::uint32_t value, std::int64_t caller_clock,
                     bool cpu) {
  switch ((address & 31) >> 2) {
  case 0:
    if (!command_.start_valid)
      command_.start = value & 0x00fffff8;
    command_.start_valid = true;
    break;
  case 1:
    command_.end = value & 0x00fffff8;
    if (command_.start_valid) {
      command_.current = command_.start;
      command_.start_valid = false;
    }
    flush_commands();
    if (sync_ && cpu)
      sync_();
    break;
  case 3:
    if (value & 1)
      command_.source = false;
    if (value & 2)
      command_.source = true;
    if (value & 4) {
      command_.freeze = false;
      flush_commands();
    }
    if (value & 8)
      command_.freeze = true;
    if (value & 16)
      command_.flush = false;
    if (value & 32)
      command_.flush = true;
    if ((value & 64) && !command_.crashed)
      command_.tmem_busy = 0;
    if ((value & 128) && !command_.crashed)
      command_.pipe_busy = 0;
    if ((value & 256) && !command_.crashed)
      command_.buffer_busy = 0;
    if (value & 512)
      command_.clock = static_cast<std::uint32_t>((clock_ - caller_clock) / 3) & 0xffffff;
    break;
  default:
    break;
  }
}

std::uint32_t Rdp::read_test(std::uint32_t address) const {
  switch ((address & 0xfffff) >> 2) {
  case 0:
    return unsigned(test_.check) | (unsigned(test_.go) << 1) | (unsigned(test_.done) << 2) |
           (unsigned(test_.fail) << 3);
  case 1:
    return unsigned(test_.enable);
  case 2:
    return test_.address;
  case 3:
    return test_.data[test_.address];
  default:
    return 0;
  }
}

void Rdp::write_test(std::uint32_t address, std::uint32_t value) {
  switch ((address & 0xfffff) >> 2) {
  case 0:
    test_.check = value & 1;
    test_.go = value & 2;
    if (value & 4)
      test_.done = false;
    break;
  case 1:
    test_.enable = value & 1;
    break;
  case 2:
    test_.address = static_cast<std::uint8_t>(value & 127);
    break;
  case 3:
    if (test_.address % 4 == 2)
      value &= 255;
    else if (test_.address % 4 == 3)
      value = 0;
    test_.data[test_.address] = value;
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
