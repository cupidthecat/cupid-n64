#pragma once

#include "core/devices/mi/mips_interface.hpp"
#include "core/devices/pi/device.hpp"
#include "core/timing/event_queue.hpp"
#include <vector>

namespace cupid::n64 {

class PeripheralInterface {
public:
  PeripheralInterface(Rdram &ram, MipsInterface &interrupts, EventQueue &events);
  void power();
  void attach(PeripheralDevice &device, unsigned priority);
  void detach(PeripheralDevice &device);
  BusRead read(std::uint32_t address, unsigned bytes);
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value);
  std::uint32_t read_io(std::uint32_t address) const;
  void write_io(std::uint32_t address, std::uint32_t value);
  void complete_dma();
  void complete_write();
  std::uint32_t force_finish_write();
  std::uint32_t dma_duration(bool read) const;

private:
  struct Domain : PeripheralTiming {
    unsigned page_size = 0;
  };
  struct Device {
    unsigned priority;
    PeripheralDevice *target;
  };
  const Domain &domain(std::uint32_t address) const;
  void select(std::uint32_t address);
  std::uint16_t read_half();
  void write_half(std::uint16_t value);
  BusRead read_word(std::uint32_t address);
  void write_word(std::uint32_t address, std::uint32_t value);
  void dma_read();
  void dma_write();
  Rdram &ram_;
  MipsInterface &interrupts_;
  EventQueue &events_;
  std::vector<Device> devices_;
  int selected_ = -1;
  PeripheralTiming timing_;
  Domain domain1_, domain2_;
  std::uint32_t dram_address_ = 0;
  std::uint32_t bus_address_ = 0;
  std::uint32_t read_length_ = 0;
  std::uint32_t write_length_ = 0;
  std::uint32_t latch_ = 0;
  bool dma_busy_ = false;
  bool io_busy_ = false;
  bool error_ = false;
  bool interrupt_ = false;
};

} // namespace cupid::n64
