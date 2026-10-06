#include "core/rsp/recompiler.hpp"
#include "core/rsp/rsp.hpp"

namespace cupid::n64 {

void Rsp::start_dma(std::int64_t clock_difference) {
  if (dma_busy() || !dma_full())
    return;
  current_ = pending_;
  busy_read_ = full_read_;
  busy_write_ = full_write_;
  full_read_ = full_write_ = false;
  dma_clock_ = clock_difference - ((current_.length + 8) / 8 * 3);
}

void Rsp::advance_dma(std::uint32_t clocks) {
  if (!dma_busy())
    return;
  dma_clock_ += clocks;
  if (dma_clock_ >= 0)
    transfer_dma();
}

void Rsp::transfer_dma() {
  const auto region = current_.local_address & 0x1000;
  if (busy_read_ && region) {
    compiler_->invalidate(current_.local_address & 0xfff, current_.length + 8);
    if (invalidate_)
      invalidate_(current_.local_address & 0xfff, current_.length + 8);
  }
  for (unsigned offset = 0; offset <= current_.length; offset += 8) {
    const auto local_address = region | (current_.local_address & 0xfff);
    if (busy_read_) {
      std::uint64_t value;
      if (region)
        value = ram_.read(current_.dram_address, 8);
      else {
        const auto upper = ram_.read(current_.dram_address, 4);
        value = (upper << 32) | ram_.read(current_.dram_address + 4, 4);
      }
      for (unsigned n = 0; n < 8; ++n)
        memory_[local_address + n] = static_cast<std::uint8_t>(value >> ((7 - n) * 8));
    }
    if (busy_write_) {
      if (region)
        ram_.write(current_.dram_address, 8, read_local(local_address, 8));
      else {
        ram_.write(current_.dram_address, 4, read_local(local_address, 4));
        ram_.write(current_.dram_address + 4, 4, read_local(local_address + 4, 4));
      }
    }
    current_.dram_address = (current_.dram_address + 8) & 0x00ffffff;
    current_.local_address = region | ((current_.local_address + 8) & 0xfff);
  }
  if (current_.count) {
    --current_.count;
    current_.dram_address = (current_.dram_address + current_.skip) & 0x00ffffff;
    dma_clock_ = -static_cast<std::int64_t>((current_.length + 8) / 8 * 3);
  } else {
    busy_read_ = busy_write_ = false;
    current_.length = 0xff8;
    start_dma(0);
  }
}

} // namespace cupid::n64
