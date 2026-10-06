#pragma once

#include "core/devices/mi/mips_interface.hpp"
#include "core/rsp/state.hpp"
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
  std::uint32_t step();
  std::uint32_t execute(std::uint32_t instruction);
  void advance(std::uint32_t clocks);
  void connect_display(std::function<std::uint32_t(unsigned)> read,
                       std::function<void(unsigned, std::uint32_t)> write);
  RspState &state() {
    return state_;
  }
  const RspState &state() const {
    return state_;
  }
  std::int64_t clocks() const {
    return clock_;
  }
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
  enum OpFlag : unsigned {
    Load = 1,
    Store = 2,
    Branch = 4,
    Vector = 8,
    NopGroup = 16,
    Bypass = 32
  };
  struct OpInfo {
    unsigned flags = 0;
    std::uint32_t read_gpr = 0, write_gpr = 0;
    std::uint32_t read_vector = 0, write_vector = 0;
    std::uint32_t read_control = 0, write_control = 0;
    std::uint32_t fake_vector = 0;
  };
  struct PipelineStage {
    std::uint32_t gpr = 0;
    std::uint32_t vector = 0;
    bool load = false;
  };
  struct Pipeline {
    std::array<PipelineStage, 3> previous{};
    OpInfo current{};
    std::uint32_t clocks = 0;
    bool single_issue = false;
    void issue(const OpInfo &op);
    void stall();
    void end();
  } pipeline_;
  static OpInfo decode_info(std::uint32_t instruction);
  static bool dual_issue(const OpInfo &first, const OpInfo &second);
  void begin_instruction();
  void end_instruction();
  void decode(std::uint32_t instruction);
  void scalar_special(std::uint32_t instruction);
  void vector_transfer(std::uint32_t instruction);
  void vector_memory(std::uint32_t instruction);
  void vector_execute(std::uint32_t instruction);
  void vector_divide(unsigned operation, unsigned dest, unsigned lane, unsigned source,
                     unsigned element);
  void take_branch(std::uint32_t address);
  std::uint32_t read_unaligned(std::uint32_t address, unsigned bytes) const;
  void write_unaligned(std::uint32_t address, unsigned bytes, std::uint32_t value);
  std::uint16_t saturate_accumulator(unsigned lane, bool middle, std::uint16_t negative,
                                     std::uint16_t positive) const;
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
  RspState state_{};
  std::uint32_t pc_ = 0;
  std::uint32_t pipeline_pc_ = 4;
  std::uint32_t next_pc_ = 4;
  std::int64_t clock_ = 0;
  bool delay_slot_ = false;
  bool next_delay_slot_ = false;
  std::int64_t dma_clock_ = 0;
  bool busy_read_ = false;
  bool busy_write_ = false;
  bool full_read_ = false;
  bool full_write_ = false;
  std::function<void()> sync_;
  std::function<void(std::uint32_t, unsigned)> invalidate_;
  std::function<std::uint32_t(unsigned)> display_read_;
  std::function<void(unsigned, std::uint32_t)> display_write_;
};

} // namespace cupid::n64
