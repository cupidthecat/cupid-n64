#include "core/disk/drive.hpp"
#include <algorithm>

namespace cupid::n64 {

void DiskDrive::command(std::uint16_t value) {
  drive_errors_ &= 0x40;
  status_ &= ~(MechaError | WriteProtected);
  if (status_ & Busy)
    return;
  status_ |= Busy;
  std::uint32_t duration = 8000;
  const auto seek = [&](unsigned destination, bool writing) {
    if (!(status_ & Present)) {
      drive_errors_ |= 1;
      return;
    }
    if ((destination >> 12) > 1 || (destination & 4095) > 0x5d7) {
      drive_errors_ |= 32;
      return;
    }
    const auto previous = unsigned(track_ & 4095), next = destination & 4095;
    duration = 993000 + 19300 * (previous > next ? previous - next : next - previous);
    if (status_ & (Retracted | Stopped))
      duration += 200700000;
    track_ = static_cast<std::uint16_t>(destination | 0x6000);
    if (writing && protected_track())
      status_ |= WriteProtected;
    events_.cancel(Event::DiskMotor);
    status_ &= ~(Retracted | Stopped);
    seeking_ = true;
  };
  switch (value) {
  case 0:
    break;
  case 1:
  case 2:
    seek(data_, value == 2);
    break;
  case 3:
  case 5:
    seek(0, false);
    break;
  case 4:
    if ((status_ & (Retracted | Stopped)) != (Retracted | Stopped))
      duration = 83000000;
    motor(2);
    break;
  case 6:
    standby_disabled_ = true;
    break;
  case 7:
    sleep_disabled_ = true;
    break;
  case 8:
    status_ &= ~Changed;
    break;
  case 9:
    status_ &= ~(Changed | Reset);
    break;
  case 10:
    data_ = data_ & 1 ? 0x5300 : 0x0114;
    break;
  case 11:
    disk_type_ = data_ & 15;
    break;
  case 12:
    data_ = static_cast<std::uint16_t>((data_ & ~127u) | drive_errors_);
    break;
  case 13:
    if (!(status_ & Present))
      drive_errors_ |= 1;
    else {
      if ((status_ & (Retracted | Stopped)) != (Retracted | Stopped))
        duration = 64000000;
      motor(1);
    }
    break;
  case 14:
    if (!(status_ & Present))
      drive_errors_ |= 1;
    break;
  case 15:
  case 16:
  case 17:
    clock_.write(value - 15, data_);
    break;
  case 18:
  case 19:
  case 20:
    data_ = clock_.read(value - 18);
    break;
  case 21:
    break;
  case 27:
    data_ = 3;
    break;
  default:
    drive_errors_ |= 16;
    break;
  }
  if ((drive_errors_ & 63) || (status_ & WriteProtected))
    status_ |= MechaError;
  events_.insert(Event::DiskResponse, duration);
}

void DiskDrive::response() {
  if (seeking_) {
    seeking_ = false;
    motor(status_ & Present ? 0 : 2);
  }
  status_ &= ~Busy;
  interrupt(false, true);
}

} // namespace cupid::n64
