#include "core/disk/drive.hpp"

namespace cupid::n64 {

bool DiskDrive::select(std::uint32_t address, PeripheralTiming timing) {
  if (!timing.permits(minimum_))
    return false;
  asic_ = false;
  if (address >= 0x05000000 && address < 0x05000400) {
    view_ = correction_;
    offset_ = address & 1023;
  } else if (address >= 0x05000400 && address < 0x05000500) {
    view_ = sector_;
    offset_ = address & 255;
  } else if (address >= 0x05000500 && address < 0x05000580) {
    asic_ = true;
    offset_ = address;
    return true;
  } else if (address >= 0x05000580 && address < 0x050005c0) {
    view_ = sequence_;
    offset_ = address & 63;
  } else if (address >= 0x06000000 && address < 0x06400000 && !ipl_.empty()) {
    view_ = ipl_;
    offset_ = address - 0x06000000;
    writable_ = false;
    return true;
  } else {
    return false;
  }
  writable_ = true;
  return true;
}

std::optional<std::uint16_t> DiskDrive::read_half(PeripheralTiming timing) {
  if (!asic_)
    return PeripheralMemory::read_half(timing);
  const auto value = read_register(offset_);
  offset_ += 2;
  return value;
}

void DiskDrive::write_half(std::uint16_t value, PeripheralTiming timing) {
  if (!asic_)
    return PeripheralMemory::write_half(value, timing);
  write_register(offset_, value);
  offset_ += 2;
}

std::uint16_t DiskDrive::read_register(unsigned offset) {
  switch (offset & 126) {
  case 0:
    return data_;
  case 8: {
    const auto status = static_cast<std::uint16_t>(status_ | (unsigned(mecha_irq_) << 9) |
                                                   (unsigned(block_irq_) << 10) |
                                                   ((block_status_ & BlockError) << 1));
    if (block_irq_) {
      events_.insert(Event::DiskBlock, 38000 + (track_ & 4095) / 15);
      interrupt(true, false);
    }
    return status;
  }
  case 12:
    return track_;
  case 16:
    return block_status_;
  case 20:
    return status_ & Present ? 0 : 1024;
  case 28:
    return static_cast<std::uint16_t>((unsigned(current_sector_) << 8) | 0xc3);
  case 40:
    return transfer_bytes_;
  case 48:
    return static_cast<std::uint16_t>((unsigned(sector_block_) << 8) | sector_bytes_);
  case 64:
    return cic() == CicModel::N8401 ? 4 : 3;
  default:
    return 0;
  }
}

void DiskDrive::write_register(unsigned offset, std::uint16_t value) {
  switch (offset & 126) {
  case 0:
    data_ = value;
    break;
  case 8:
    command(value);
    break;
  case 16:
    block_reset_ |= (value & 4096) != 0;
    reading_ = value & 16384;
    block_status_ = static_cast<std::uint16_t>((block_status_ & ~Transfer) | ((value & 512) >> 1));
    if (value & 256)
      interrupt(false, false);
    current_sector_ = static_cast<std::uint8_t>(value);
    if (!(value & 4096) && block_reset_) {
      block_status_ &= ~(Running | BlockError);
      status_ &= ~(SectorReady | C2Ready);
      block_reset_ = false;
      interrupt(true, false);
    }
    if ((value & 32768) && !disk_.empty()) {
      block_status_ |= Running;
      events_.insert(Event::DiskBlock, 50000 + (track_ & 4095) / 15);
    }
    break;
  case 32:
    if (value == 0xaaaa)
      power();
    break;
  case 40:
    transfer_bytes_ = static_cast<std::uint8_t>(value);
    break;
  case 44:
    sector_bytes_ = static_cast<std::uint8_t>(value);
    sector_block_ = static_cast<std::uint8_t>(value >> 8);
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
