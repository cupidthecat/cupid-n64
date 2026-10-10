#pragma once

#include "core/devices/cic/cic.hpp"
#include "core/devices/pi/device.hpp"
#include "core/disk/clock/clock.hpp"
#include "core/timing/event_queue.hpp"
#include <vector>

namespace cupid::n64 {

class DiskDrive final : public PeripheralMemory {
public:
  explicit DiskDrive(EventQueue &events, DiskClock::HostClock clock = {});
  void connect_interrupt(std::function<void(bool)> callback);
  bool load_ipl(std::span<const std::uint8_t> firmware);
  bool insert(std::span<const std::uint8_t> physical, std::span<const std::uint8_t> errors = {});
  bool load_image(std::span<const std::uint8_t> image);
  void eject();
  void power();
  void event(Event event);
  bool firmware_loaded() const;
  CicModel cic() const;
  std::span<std::uint8_t> disk_data();
  std::span<const std::uint8_t> disk_errors() const;
  DiskClock &clock();
  bool select(std::uint32_t address, PeripheralTiming timing) override;
  std::optional<std::uint16_t> read_half(PeripheralTiming timing) override;
  void write_half(std::uint16_t value, PeripheralTiming timing) override;
  std::uint16_t read_register(unsigned offset);
  void write_register(unsigned offset, std::uint16_t value);

private:
  friend class CoreState;
  enum Status : unsigned {
    Changed = 1,
    MechaError = 2,
    WriteProtected = 4,
    Retracted = 8,
    Stopped = 16,
    Reset = 64,
    Busy = 128,
    Present = 256,
    C2Ready = 4096,
    SectorReady = 16384
  };
  enum BlockStatus : unsigned {
    C1Single = 32,
    C1Double = 64,
    Transfer = 256,
    BlockError = 1024,
    Running = 32768
  };
  void command(std::uint16_t value);
  void response();
  void block_request();
  bool protected_track() const;
  void motor(unsigned mode);
  void interrupt(bool block, bool line);
  EventQueue &events_;
  DiskClock clock_;
  std::function<void(bool)> interrupt_;
  std::vector<std::uint8_t> ipl_, disk_, errors_;
  std::array<std::uint8_t, 1024> correction_{};
  std::array<std::uint8_t, 256> sector_{};
  std::array<std::uint8_t, 64> sequence_{};
  bool asic_ = false;
  bool mecha_irq_ = false, block_irq_ = false;
  bool seeking_ = false, block_reset_ = false, reading_ = false;
  bool standby_disabled_ = false, sleep_disabled_ = false;
  std::uint16_t data_ = 0, track_ = 0, status_ = 0, block_status_ = 0;
  std::uint8_t current_sector_ = 0, sector_bytes_ = 0, transfer_bytes_ = 0, sector_block_ = 0;
  unsigned disk_type_ = 0, drive_errors_ = 0;
};

} // namespace cupid::n64
