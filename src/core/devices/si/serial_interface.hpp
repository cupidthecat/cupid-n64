#pragma once

#include "core/devices/mi/mips_interface.hpp"
#include "core/devices/pif/pif.hpp"
#include "core/timing/event_queue.hpp"

namespace cupid::n64 {

class SerialInterface {
public:
  SerialInterface(Pif &pif, MipsInterface &interrupts, EventQueue &events);
  void power();
  BusRead read(std::uint32_t address, unsigned bytes);
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value);
  std::uint32_t read_io(std::uint32_t address) const;
  void write_io(std::uint32_t address, std::uint32_t value);
  void dma_read();
  void dma_write();
  void complete_write();
  void connect_sync(std::function<void()> callback);

private:
  std::uint32_t read_word(std::uint32_t address);
  void write_word(std::uint32_t address, std::uint32_t value);
  void complete_dma();
  Pif &pif_;
  MipsInterface &interrupts_;
  EventQueue &events_;
  std::function<void()> sync_;
  std::uint32_t dram_address_ = 0;
  std::uint32_t read_address_ = 0;
  std::uint32_t write_address_ = 0;
  std::uint32_t latch_ = 0;
  unsigned pch_state_ = 0;
  unsigned dma_state_ = 0;
  bool dma_busy_ = false;
  bool io_busy_ = false;
  bool interrupt_ = false;
};

} // namespace cupid::n64
