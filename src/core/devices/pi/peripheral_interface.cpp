#include "core/devices/pi/peripheral_interface.hpp"
#include <algorithm>

namespace cupid::n64 {

PeripheralInterface::PeripheralInterface(Rdram &ram, MipsInterface &interrupts, EventQueue &events)
    : ram_(ram), interrupts_(interrupts), events_(events) {
  power();
}

void PeripheralInterface::power() {
  dram_address_ = bus_address_ = read_length_ = write_length_ = latch_ = 0;
  dma_busy_ = io_busy_ = error_ = interrupt_ = false;
  domain1_ = {};
  domain2_ = {};
  selected_ = -1;
  timing_ = {};
}

void PeripheralInterface::attach(PeripheralDevice &device, unsigned priority) {
  const auto position = std::find_if(devices_.begin(), devices_.end(), [&](const Device &entry) {
    return entry.priority > priority;
  });
  const auto index = static_cast<int>(position - devices_.begin());
  if (selected_ >= index)
    ++selected_;
  devices_.insert(position, {priority, &device});
}

void PeripheralInterface::detach(PeripheralDevice &device) {
  for (unsigned n = 0; n < devices_.size(); ++n) {
    if (devices_[n].target != &device)
      continue;
    devices_.erase(devices_.begin() + n);
    if (selected_ == static_cast<int>(n))
      selected_ = -1;
    else if (selected_ > static_cast<int>(n))
      --selected_;
    return;
  }
}

const PeripheralInterface::Domain &PeripheralInterface::domain(std::uint32_t address) const {
  const auto region = address >> 24;
  return region == 5 || (region >= 8 && region <= 15) ? domain2_ : domain1_;
}

void PeripheralInterface::select(std::uint32_t address) {
  address &= ~1u;
  latch_ = (address & 0xffff) * 0x10001u;
  timing_ = domain(address);
  selected_ = -1;
  for (unsigned n = 0; n < devices_.size(); ++n) {
    if (devices_[n].target->select(address, timing_)) {
      selected_ = static_cast<int>(n);
      return;
    }
  }
}

std::uint16_t PeripheralInterface::read_half() {
  if (selected_ >= 0) {
    if (const auto value = devices_[selected_].target->read_half(timing_))
      latch_ = std::uint32_t(*value) * 0x10001u;
  }
  return static_cast<std::uint16_t>(latch_);
}

void PeripheralInterface::write_half(std::uint16_t value) {
  if (!io_busy_)
    latch_ = std::uint32_t(value) * 0x10001u;
  if (selected_ >= 0)
    devices_[selected_].target->write_half(value, timing_);
}

BusRead PeripheralInterface::read_word(std::uint32_t address) {
  if (address <= 0x046fffff)
    return {read_io(address)};
  if (io_busy_) {
    io_busy_ = false;
    return {latch_, events_.cancel(Event::PeripheralBusWrite) * 2};
  }
  select(address);
  const auto upper = std::uint32_t(read_half()) << 16;
  latch_ = upper | read_half();
  bus_address_ = (address + 4) & ~1u;
  return {latch_, 500};
}

void PeripheralInterface::write_word(std::uint32_t address, std::uint32_t value) {
  if (address <= 0x046fffff)
    return write_io(address, value);
  if (io_busy_)
    return;
  io_busy_ = true;
  bus_address_ = (address + 4) & ~1u;
  events_.insert(Event::PeripheralBusWrite, 400);
  select(address);
  latch_ = value;
  write_half(static_cast<std::uint16_t>(value >> 16));
  write_half(static_cast<std::uint16_t>(value));
}

BusRead PeripheralInterface::read(std::uint32_t address, unsigned bytes) {
  auto result = read_word(address);
  result.clocks += 40;
  if (bytes == 8) {
    const auto second = read_word(address + 4);
    result.value = (result.value << 32) | second.value;
    result.clocks += second.clocks;
  } else if (bytes != 4)
    result.value >>= (4 - bytes - (address & (4 - bytes))) * 8;
  return result;
}

BusWrite PeripheralInterface::write(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  auto word = static_cast<std::uint32_t>(bytes == 8 ? value >> 32 : value);
  if (bytes != 4 && bytes != 8)
    word <<= (4 - bytes - (address & (4 - bytes))) * 8;
  write_word(address, word);
  return {};
}

void PeripheralInterface::complete_write() {
  io_busy_ = false;
}

void PeripheralInterface::complete_dma() {
  dma_busy_ = false;
  interrupt_ = true;
  interrupts_.raise(Interrupt::Peripheral);
}

std::uint32_t PeripheralInterface::read_io(std::uint32_t address) const {
  const auto index = (address & 63) >> 2;
  if (index == 0)
    return dram_address_;
  if (index == 1)
    return bus_address_;
  if (index == 2)
    return read_length_;
  if (index == 3)
    return write_length_;
  if (index == 4)
    return unsigned(dma_busy_) | (unsigned(io_busy_) << 1) | (unsigned(error_) << 2) |
           (unsigned(interrupt_) << 3);
  if (index >= 5 && index <= 12) {
    const auto &settings = index < 9 ? domain1_ : domain2_;
    switch ((index - 5) & 3) {
    case 0:
      return settings.latency;
    case 1:
      return settings.pulse_width;
    case 2:
      return settings.page_size;
    default:
      return settings.release;
    }
  }
  return index == 13 || index == 14 ? latch_ : 0;
}

void PeripheralInterface::write_io(std::uint32_t address, std::uint32_t value) {
  const auto index = (address & 63) >> 2;
  if (index != 4 && (dma_busy_ || io_busy_)) {
    error_ = true;
    return;
  }
  if (index == 0)
    dram_address_ = value & 0x00fffffe;
  if (index == 1)
    bus_address_ = value & ~1u;
  if (index == 2) {
    read_length_ = value & 0xffffff;
    dma_busy_ = true;
    events_.insert(Event::PeripheralRead, dma_duration(true));
    dma_read();
  }
  if (index == 3) {
    write_length_ = value & 0xffffff;
    dma_busy_ = true;
    events_.insert(Event::PeripheralWrite, dma_duration(false));
    dma_write();
  }
  if (index == 4) {
    if (value & 1) {
      dma_busy_ = error_ = false;
      events_.cancel(Event::PeripheralRead);
      events_.cancel(Event::PeripheralWrite);
    }
    if (value & 2) {
      interrupt_ = false;
      interrupts_.lower(Interrupt::Peripheral);
    }
  }
  if (index >= 5 && index <= 12) {
    auto &settings = index < 9 ? domain1_ : domain2_;
    switch ((index - 5) & 3) {
    case 0:
      settings.latency = value & 255;
      break;
    case 1:
      settings.pulse_width = value & 255;
      break;
    case 2:
      settings.page_size = value & 15;
      break;
    default:
      settings.release = value & 3;
      break;
    }
  }
}

} // namespace cupid::n64
