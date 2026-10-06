#pragma once

#include "core/devices/mi/mips_interface.hpp"
#include <functional>

namespace cupid::n64 {

struct RspStatus {
  bool semaphore = false;
  bool halted = true;
  bool broken = false;
  bool io_full = false;
  bool single_step = false;
  bool interrupt_on_break = false;
  std::uint8_t signals = 0;
};

class Rsp {
public:
  Rsp(Rdram &ram, MipsInterface &interrupts, RandomGenerator &random);
  void power();
  void connect_sync(std::function<void()> callback);
  void connect_invalidation(std::function<void(std::uint32_t, unsigned)> callback);
  BusRead read(std::uint32_t address, unsigned bytes);
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value,
                 std::int64_t clock_difference = 0);
  std::uint32_t read_word(std::uint32_t address);
  void write_word(std::uint32_t address, std::uint32_t value, std::int64_t clock_difference = 0);
  std::uint32_t read_io(std::uint32_t address);
  void write_io(std::uint32_t address, std::uint32_t value, std::int64_t clock_difference = 0);
  std::uint32_t read_status(std::uint32_t address);
  void write_status(std::uint32_t address, std::uint32_t value);
  std::uint64_t read_local(std::uint32_t address, unsigned bytes) const;
  void write_local(std::uint32_t address, unsigned bytes, std::uint64_t value);
  void advance_dma(std::uint32_t clocks);
  const RspStatus &status() const {
    return status_;
  }
  std::uint32_t pc() const {
    return pc_;
  }
  std::span<std::uint8_t, 4096> dmem() {
    return std::span(memory_).first<4096>();
  }
  std::span<std::uint8_t, 4096> imem() {
    return std::span(memory_).last<4096>();
  }
  bool dma_busy() const {
    return busy_read_ || busy_write_;
  }
  std::int64_t dma_clocks() const {
    return dma_clock_;
  }

private:
  struct DmaRegisters {
    std::uint32_t local_address = 0;
    std::uint32_t dram_address = 0;
    std::uint32_t length = 0;
    std::uint32_t skip = 0;
    std::uint32_t count = 0;
  } pending_, current_;
  void start_dma(std::int64_t clock_difference);
  void transfer_dma();
  bool dma_full() const {
    return full_read_ || full_write_;
  }

  Rdram &ram_;
  MipsInterface &interrupts_;
  RandomGenerator &random_;
  std::array<std::uint8_t, 8192> memory_{};
  RspStatus status_{};
  std::uint32_t pc_ = 0;
  std::int64_t dma_clock_ = 0;
  bool busy_read_ = false;
  bool busy_write_ = false;
  bool full_read_ = false;
  bool full_write_ = false;
  std::function<void()> sync_;
  std::function<void(std::uint32_t, unsigned)> invalidate_;
};

} // namespace cupid::n64
