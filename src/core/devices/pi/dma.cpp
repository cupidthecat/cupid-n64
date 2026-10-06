#include "core/devices/pi/peripheral_interface.hpp"
#include <algorithm>
#include <array>

namespace cupid::n64 {

void PeripheralInterface::dma_read() {
  read_length_ = (read_length_ | 1) + 1;
  const auto page_mask = (1u << (domain(bus_address_).page_size + 2)) - 1;
  select(bus_address_);
  for (std::uint32_t offset = 0; offset < read_length_; offset += 2) {
    const auto address = bus_address_ + offset;
    if (offset && !(address & page_mask))
      select(address);
    write_half(static_cast<std::uint16_t>(ram_.read(dram_address_ + offset, 2)));
  }
}

void PeripheralInterface::dma_write() {
  std::array<std::uint8_t, 128> buffer{};
  int remaining = static_cast<int>(write_length_) + 1;
  int maximum = 128;
  bool first = true;
  bool selected = false;
  const auto page_mask = (1u << (domain(bus_address_).page_size + 2)) - 1;
  while (remaining > 0) {
    const int misalignment = dram_address_ & 7;
    const int row_remaining = 0x800 - (dram_address_ & 0x7ff);
    const int block = std::min(maximum - misalignment, row_remaining);
    const int length = std::min(remaining, block);
    for (int n = 0; n < length; n += 2) {
      if (!selected || !(bus_address_ & page_mask)) {
        select(bus_address_);
        selected = true;
      }
      const auto value = read_half();
      buffer[n] = static_cast<std::uint8_t>(value >> 8);
      buffer[n + 1] = static_cast<std::uint8_t>(value);
      bus_address_ += 2;
      remaining -= 2;
    }
    if (first && length < 127 - misalignment) {
      for (int n = 0; n < length - misalignment; ++n)
        ram_.write(dram_address_++, 1, buffer[n]);
    } else {
      for (int n = 0; n < length - misalignment; n += 2) {
        ram_.write(dram_address_++, 1, buffer[n]);
        ram_.write(dram_address_++, 1, buffer[n + 1]);
      }
    }
    dram_address_ = (dram_address_ + 7) & ~7u;
    write_length_ = length <= 8 ? 127 - misalignment : 127;
    first = false;
    maximum = row_remaining < 8 ? 128 - misalignment : 128;
  }
}

std::uint32_t PeripheralInterface::dma_duration(bool read) const {
  const auto length = ((read ? read_length_ : write_length_) | 1) + 1;
  const auto &settings = domain(bus_address_);
  const auto page_shift = settings.page_size + 2;
  const auto page_size = 1u << page_shift;
  const auto page_mask = page_size - 1;
  const auto first_page = bus_address_ >> page_shift;
  const auto last_address = bus_address_ + length - 2;
  const auto last_page = last_address >> page_shift;
  const auto pages = last_page - first_page + 1;
  unsigned buffers = 0;
  unsigned partial = 0;
  if (first_page == last_page) {
    if (length == 128)
      buffers = 1;
    else
      partial = length;
  } else {
    if (!(bus_address_ & page_mask))
      ++buffers;
    else
      partial += page_size - (bus_address_ & page_mask);
    if (!((last_address + 2) & page_mask))
      ++buffers;
    else
      partial += (last_address & page_mask) + 2;
    if (first_page + 1 < last_page)
      buffers += (pages - 2) * page_size / 128;
  }
  const auto cycles = (settings.latency + 15) * pages +
                      (settings.pulse_width + settings.release + 2) * length / 2 + buffers * 28 +
                      partial;
  return cycles * 3;
}

} // namespace cupid::n64
