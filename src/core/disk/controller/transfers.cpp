#include "core/disk/drive.hpp"
#include "core/disk/image/geometry.hpp"

namespace cupid::n64 {

bool DiskDrive::protected_track() const {
  return disk_geometry::track_zone(track_ & 4095) + ((track_ >> 12) & 1) <= disk_type_ + 2;
}

void DiskDrive::motor(unsigned mode) {
  events_.cancel(Event::DiskMotor);
  status_ &= ~(Retracted | Stopped);
  if (mode >= 1)
    status_ |= Retracted;
  if (mode >= 2)
    status_ |= Stopped;
  if (!mode && !standby_disabled_)
    events_.insert(Event::DiskMotor, (187500000 / 23) * 69);
  if (mode == 1 && !sleep_disabled_)
    events_.insert(Event::DiskMotor, (187500000 / 23) * 23);
}

void DiskDrive::block_request() {
  if (!(block_status_ & Running)) {
    events_.cancel(Event::DiskBlock);
    interrupt(true, false);
    return;
  }
  status_ &= ~(SectorReady | C2Ready);
  block_status_ &= ~(BlockError | C1Single | C1Double | 512);
  const auto block = unsigned(current_sector_ >= 90);
  const auto sector = unsigned(current_sector_) - block * 90;
  const auto track = unsigned(track_ & 4095), head = unsigned((track_ >> 12) & 1);
  const auto copy = [&](unsigned source_sector, bool writing) {
    const auto offset =
        disk_geometry::sector_offset(track, head, source_sector, transfer_bytes_ + 1);
    for (unsigned byte = 0; byte <= transfer_bytes_; ++byte) {
      if (writing) {
        if (offset + byte < disk_.size())
          disk_[offset + byte] = sector_[byte];
      } else {
        sector_[byte] = offset + byte < disk_.size() ? disk_[offset + byte] : 255;
      }
    }
  };
  const auto finish = [&] {
    if (block_status_ & Transfer) {
      block_status_ &= ~Transfer;
      current_sector_ = static_cast<std::uint8_t>((1 - block) * 90);
    } else {
      block_status_ &= ~Running;
    }
  };
  if (reading_) {
    const auto error = head * 2350 + track * 2 + block;
    if (error < errors_.size() && errors_[error])
      block_status_ |= C1Single | C1Double;
    if (sector < 85) {
      copy(current_sector_, false);
      status_ |= SectorReady;
      ++current_sector_;
    } else if (sector < 88) {
      ++current_sector_;
    } else if (sector == 88) {
      status_ |= C2Ready;
      finish();
    }
  } else {
    if (sector <= 85) {
      if (sector)
        copy(current_sector_ - 1, true);
      status_ |= SectorReady;
    }
    if (sector >= 85) {
      finish();
      if (!(block_status_ & Running))
        status_ &= ~SectorReady;
    }
    ++current_sector_;
  }
  interrupt(true, true);
}

} // namespace cupid::n64
