#include "core/devices/si/serial_interface.hpp"
#include <utility>

namespace cupid::n64 {

SerialInterface::SerialInterface(Pif &pif, MipsInterface &interrupts, EventQueue &events)
    : pif_(pif), interrupts_(interrupts), events_(events) {
  power();
}

void SerialInterface::power() {
  dram_address_ = read_address_ = write_address_ = latch_ = 0;
  pch_state_ = dma_state_ = 0;
  dma_busy_ = io_busy_ = interrupt_ = false;
}

void SerialInterface::connect_sync(std::function<void()> callback) {
  sync_ = std::move(callback);
}

std::uint32_t SerialInterface::read_io(std::uint32_t address) const {
  switch ((address & 31) >> 2) {
  case 0:
    return dram_address_;
  case 1:
    return read_address_;
  case 4:
    return write_address_;
  case 6:
    if (sync_)
      sync_();
    return unsigned(dma_busy_) | (unsigned(io_busy_) << 1) | (pch_state_ << 4) | (dma_state_ << 8) |
           (unsigned(interrupt_) << 12);
  default:
    return 0;
  }
}

void SerialInterface::write_io(std::uint32_t address, std::uint32_t value) {
  switch ((address & 31) >> 2) {
  case 0:
    dram_address_ = value & 0x00fffff8;
    break;
  case 1:
    read_address_ = value & ~1u;
    dma_busy_ = true;
    dma_state_ = 1;
    pch_state_ = 4;
    events_.insert(Event::SerialRead, pif_.estimate_timing() * 3);
    break;
  case 4:
    write_address_ = value & ~1u;
    dma_busy_ = true;
    dma_state_ = 4;
    pch_state_ = 1;
    events_.insert(Event::SerialWrite, 4065 * 3);
    break;
  case 6:
    interrupt_ = false;
    interrupts_.lower(Interrupt::Serial);
    break;
  default:
    break;
  }
}

std::uint32_t SerialInterface::read_word(std::uint32_t address) {
  if (address <= 0x048fffff)
    return read_io(address);
  if (io_busy_) {
    io_busy_ = false;
    events_.cancel(Event::SerialBusWrite);
    return latch_;
  }
  return pif_.read_word(address);
}

void SerialInterface::write_word(std::uint32_t address, std::uint32_t value) {
  if (address <= 0x048fffff)
    return write_io(address, value);
  if (io_busy_)
    return;
  io_busy_ = dma_busy_ = true;
  pch_state_ = 11;
  dma_state_ = 9;
  latch_ = value;
  events_.insert(Event::SerialBusWrite, 2150 * 3);
  pif_.write_word(address, value);
}

BusRead SerialInterface::read(std::uint32_t address, unsigned bytes) {
  std::uint64_t value = read_word(address);
  if (bytes == 8)
    value = (value << 32) | read_word(address + 4);
  else if (bytes != 4)
    value >>= (4 - bytes - (address & (4 - bytes))) * 8;
  return {value, 40};
}

BusWrite SerialInterface::write(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  auto word = static_cast<std::uint32_t>(bytes == 8 ? value >> 32 : value);
  if (bytes != 4 && bytes != 8)
    word <<= (4 - bytes - (address & (4 - bytes))) * 8;
  write_word(address, word);
  return {};
}

void SerialInterface::complete_dma() {
  dma_busy_ = false;
  pch_state_ = dma_state_ = 0;
  interrupt_ = true;
  interrupts_.raise(Interrupt::Serial);
}

void SerialInterface::complete_write() {
  io_busy_ = false;
  complete_dma();
}

void SerialInterface::dma_read() {
  pif_.dma_read(read_address_, dram_address_);
  complete_dma();
}

void SerialInterface::dma_write() {
  pif_.dma_write(write_address_, dram_address_);
  complete_dma();
}

} // namespace cupid::n64
