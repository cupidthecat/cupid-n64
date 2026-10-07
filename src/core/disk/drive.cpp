#include "drive.hpp"
#include "image/geometry.hpp"
#include <algorithm>

namespace cupid::n64 {

DiskDrive::DiskDrive(EventQueue &events, DiskClock::HostClock clock)
    : events_(events), clock_(std::move(clock)) {}

void DiskDrive::connect_interrupt(std::function<void(bool)> callback) {
  interrupt_ = std::move(callback);
  if (interrupt_)
    interrupt_(mecha_irq_ || block_irq_);
}

bool DiskDrive::load_ipl(std::span<const std::uint8_t> firmware) {
  if (firmware.size() < 4096 || firmware.size() > 0x400000 || (firmware.size() & 3))
    return false;
  unsigned swap = 0;
  if ((firmware[0] == 0x37 || firmware[0] == 0x27) && firmware[1] == 0x80)
    swap = 1;
  else if (firmware[0] == 0x40 && firmware[3] == 0x80)
    swap = 3;
  else if (firmware[0] != 0x80 || (firmware[1] != 0x37 && firmware[1] != 0x27))
    return false;
  ipl_.assign(0x400000, 255);
  for (unsigned byte = 0; byte < firmware.size(); ++byte)
    ipl_[byte] = firmware[byte ^ swap];
  view_ = {};
  return true;
}

bool DiskDrive::insert(std::span<const std::uint8_t> physical,
                       std::span<const std::uint8_t> errors) {
  if (physical.size() != disk_geometry::PhysicalSize || (!errors.empty() && errors.size() != 4700))
    return false;
  std::vector<std::uint8_t> next_disk(physical.begin(), physical.end());
  std::vector<std::uint8_t> next_errors(4700, 0);
  if (!errors.empty())
    std::copy(errors.begin(), errors.end(), next_errors.begin());
  disk_.swap(next_disk);
  errors_.swap(next_errors);
  status_ |= Changed | Present;
  return true;
}

void DiskDrive::eject() {
  disk_.clear();
  status_ &= ~Present;
  if (status_ & Busy)
    status_ |= MechaError;
  if (block_status_ & Running)
    block_status_ = static_cast<std::uint16_t>((block_status_ & ~Running) | BlockError);
  motor(2);
}

void DiskDrive::power() {
  correction_.fill(0);
  sector_.fill(0);
  sequence_.fill(0);
  mecha_irq_ = block_irq_ = seeking_ = block_reset_ = reading_ = false;
  standby_disabled_ = sleep_disabled_ = false;
  data_ = track_ = block_status_ = 0;
  current_sector_ = sector_bytes_ = transfer_bytes_ = sector_block_ = 0;
  disk_type_ = drive_errors_ = 0;
  status_ = static_cast<std::uint16_t>(Reset | Changed | (disk_.empty() ? 0u : unsigned(Present)));
  motor(2);
  events_.insert(Event::DiskClock, 187500000);
  events_.cancel(Event::DiskResponse);
  events_.cancel(Event::DiskBlock);
  interrupt(false, false);
  interrupt(true, false);
}

void DiskDrive::interrupt(bool block, bool line) {
  (block ? block_irq_ : mecha_irq_) = line;
  if (interrupt_)
    interrupt_(mecha_irq_ || block_irq_);
}

bool DiskDrive::firmware_loaded() const {
  return !ipl_.empty();
}

CicModel DiskDrive::cic() const {
  if (ipl_.size() >= 64) {
    if (std::equal(ipl_.begin() + 0x3b, ipl_.begin() + 0x3f, "NDXJ"))
      return CicModel::N8401;
    if (std::equal(ipl_.begin() + 0x3b, ipl_.begin() + 0x3f, "NDDE"))
      return CicModel::Ddus;
  }
  return CicModel::N8303;
}

std::span<std::uint8_t> DiskDrive::disk_data() {
  return disk_;
}
std::span<const std::uint8_t> DiskDrive::disk_errors() const {
  return errors_;
}
DiskClock &DiskDrive::clock() {
  return clock_;
}

void DiskDrive::event(Event event) {
  switch (event) {
  case Event::DiskClock:
    clock_.tick();
    events_.cancel(Event::DiskClock);
    events_.insert(Event::DiskClock, 187500000);
    break;
  case Event::DiskResponse:
    response();
    break;
  case Event::DiskBlock:
    block_request();
    break;
  case Event::DiskMotor:
    if (status_ & Retracted)
      motor(2);
    else if (!(status_ & Stopped))
      motor(1);
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
