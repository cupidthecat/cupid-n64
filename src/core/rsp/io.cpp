#include "core/rsp/rsp.hpp"

namespace cupid::n64 {

std::uint32_t Rsp::read_word(std::uint32_t address) {
  if (address <= 0x0403ffff)
    return static_cast<std::uint32_t>(read_local(address, 4));
  if (address <= 0x0407ffff)
    return read_io(address);
  return read_status(address);
}

void Rsp::write_word(std::uint32_t address, std::uint32_t value, std::int64_t clock_difference) {
  if (address <= 0x0403ffff)
    return write_local(address, 4, value);
  if (address <= 0x0407ffff)
    return write_io(address, value, clock_difference);
  write_status(address, value);
}

std::uint32_t Rsp::read_io(std::uint32_t address) {
  switch ((address & 31) >> 2) {
  case 0:
    return current_.local_address;
  case 1:
    return current_.dram_address;
  case 2:
  case 3:
    return current_.length | (current_.count << 12) | (current_.skip << 20);
  case 4:
    if (sync_)
      sync_();
    return unsigned(status_.halted) | (unsigned(status_.broken) << 1) |
           (unsigned(dma_busy()) << 2) | (unsigned(dma_full()) << 3) |
           (unsigned(status_.io_full) << 4) | (unsigned(status_.single_step) << 5) |
           (unsigned(status_.interrupt_on_break) << 6) | (unsigned(status_.signals) << 7);
  case 5:
    return dma_full();
  case 6:
    return dma_busy();
  case 7: {
    const auto value = status_.semaphore;
    status_.semaphore = true;
    if (sync_)
      sync_();
    return value;
  }
  default:
    return 0;
  }
}

void Rsp::write_io(std::uint32_t address, std::uint32_t value, std::int64_t clock_difference) {
  switch ((address & 31) >> 2) {
  case 0:
    pending_.local_address = value & 0x1ff8;
    break;
  case 1:
    pending_.dram_address = value & 0x00fffff8;
    break;
  case 2:
  case 3:
    pending_.length = value & 0xff8;
    pending_.count = (value >> 12) & 255;
    pending_.skip = (value >> 20) & 0xff8;
    full_read_ = (address & 4) == 0;
    full_write_ = !full_read_;
    start_dma(clock_difference);
    break;
  case 4: {
    const auto update = [value](bool &flag, unsigned bit) {
      const auto pair = (value >> bit) & 3;
      if (pair == 1)
        flag = false;
      if (pair == 2)
        flag = true;
    };
    update(status_.halted, 0);
    if (value & 4)
      status_.broken = false;
    if ((value & 24) == 8)
      interrupts_.lower(Interrupt::Signal);
    if ((value & 24) == 16)
      interrupts_.raise(Interrupt::Signal);
    update(status_.single_step, 5);
    update(status_.interrupt_on_break, 7);
    for (unsigned n = 0; n < 8; ++n) {
      bool signal = status_.signals & (1u << n);
      update(signal, 9 + n * 2);
      status_.signals =
          static_cast<std::uint8_t>((status_.signals & ~(1u << n)) | (unsigned(signal) << n));
    }
    if (sync_)
      sync_();
    break;
  }
  case 7:
    status_.semaphore = false;
    if (sync_)
      sync_();
    break;
  default:
    break;
  }
}

std::uint32_t Rsp::read_status(std::uint32_t address) {
  if (((address & 31) >> 2) == 0)
    return status_.halted ? pc_ : static_cast<std::uint32_t>(random_() & 0xfff);
  return 0;
}

void Rsp::write_status(std::uint32_t address, std::uint32_t value) {
  if (((address & 31) >> 2) == 0) {
    pc_ = value & 0xffc;
    pipeline_pc_ = pc_;
    next_pc_ = (pc_ + 4) & 0xfff;
    delay_slot_ = next_delay_slot_ = false;
  }
}

} // namespace cupid::n64
