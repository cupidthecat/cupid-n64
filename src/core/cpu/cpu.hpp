#pragma once

#include "core/cpu/state.hpp"
#include "core/memory/bus.hpp"
#include "core/timing/random.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace cupid::n64 {

class CpuCompiler;

class Cpu {
public:
  explicit Cpu(Bus &bus, RandomGenerator *random = nullptr);
  ~Cpu();
  void power();
  void step();
  void execute(std::uint32_t instruction);
  void set_pc(std::uint64_t address);
  void set_interrupt(unsigned bit, bool pending);
  void request_nmi();
  void advance_clocks(std::uint64_t clocks);
  void synchronize_timer(const std::function<void()> &devices = {});
  bool run_block(const std::uint64_t &clock_target);
  bool run_interpreted_block(const std::uint64_t &clock_target);
  std::uint64_t synchronization_limit() const;
  void connect_sync(std::function<void()> callback) {
    synchronize_ = std::move(callback);
  }
  std::uint64_t read_control(unsigned index);
  void write_control(unsigned index, std::uint64_t value);

  CpuState &state() {
    return state_;
  }
  const CpuState &state() const {
    return state_;
  }
  bool in_delay_slot() const {
    return delay_slot_;
  }

private:
  friend class CpuCompiler;
  enum class Mode { Kernel, Supervisor, User };
  enum class Segment { Invalid, Mapped, Cached, Direct, Cached32, Direct32 };

  struct CacheLine {
    std::array<std::uint32_t, 8> words{};
    std::uint32_t tag = 0;
    bool valid = false;
    bool dirty = false;
    bool hit(std::uint32_t address) const {
      return valid && tag == (address & ~0xfffu);
    }
  };

  Mode mode() const;
  bool extended_addressing() const;
  bool little_endian() const;
  bool reverse_endian() const;
  bool native_cache_hit(std::uint32_t instruction, std::uint64_t ram_bytes) const;
  bool require_doubleword();
  bool poll_interrupt();
  void begin_instruction();
  void end_instruction();
  void decode(std::uint32_t instruction);
  void special(std::uint32_t instruction);
  void regimm(std::uint32_t instruction);
  void cop0(std::uint32_t instruction);
  void cop1(std::uint32_t instruction);
  void cop2(std::uint32_t instruction);
  void cop2_invalid();
  bool fpu_enabled();
  bool fpu_begin();
  bool fpu_exception(unsigned exceptions);
  bool fpu_host_exceptions(unsigned exceptions, bool conversion = false);
  bool fpu_unimplemented();
  unsigned fpu_source(unsigned index) const;
  std::uint64_t fpu_transfer(unsigned index, bool wide) const;
  void fpu_transfer(unsigned index, bool wide, std::uint64_t value);
  void fpu_memory(unsigned operation, unsigned reg, std::uint64_t address);
  template <typename Float> void fpu_arithmetic(std::uint32_t instruction);
  template <typename Float>
  void fpu_to_integer(unsigned dest, unsigned source, bool wide, unsigned rounding);
  template <typename Float> void fpu_convert(unsigned dest, unsigned source, unsigned format);
  template <typename Float> bool fpu_inputs(Float a, std::optional<Float> b = {});
  template <typename Float> bool fpu_output(Float &value);
  void branch(bool taken, bool likely, std::int16_t offset);
  void jump(std::uint64_t target);
  void raise(Exception code, unsigned coprocessor = 0, bool tlb_miss = false);
  void address_exception(std::uint64_t address);
  void add(unsigned dest, std::uint64_t a, std::uint64_t b, bool wide, bool trap);
  void subtract(unsigned dest, std::uint64_t a, std::uint64_t b, bool wide, bool trap);
  unsigned multiply(std::uint64_t a, std::uint64_t b, bool wide, bool is_signed);
  unsigned divide(std::uint64_t a, std::uint64_t b, bool wide, bool is_signed);
  Segment segment(std::uint64_t address) const;
  std::optional<Address> translate(std::uint64_t address, unsigned bytes, bool store,
                                   bool alignment = true);
  std::optional<std::uint64_t> read(std::uint64_t address, unsigned bytes,
                                    bool instruction = false);
  bool write(std::uint64_t address, unsigned bytes, std::uint64_t value, bool alignment = true);
  void load_store(std::uint32_t instruction);
  void load_linked(unsigned reg, std::uint64_t address, bool wide);
  void load_merge(unsigned reg, std::uint64_t address, unsigned bytes, bool left);
  void store_merge(std::uint64_t value, std::uint64_t address, unsigned bytes, bool left);
  std::optional<std::uint64_t> cache_read(std::uint64_t virtual_address, std::uint32_t physical,
                                          unsigned bytes, bool instruction);
  bool cache_write(std::uint64_t virtual_address, std::uint32_t physical, unsigned bytes,
                   std::uint64_t value);
  bool fill(CacheLine &line, std::uint32_t physical, std::uint32_t index, bool instruction);
  bool writeback(CacheLine &line, std::uint32_t index, bool instruction);
  void cache_operation(unsigned operation, std::uint64_t address);
  unsigned random_index();
  void write_tlb(unsigned index);
  void interrupt_changed();
  void commit_count(std::uint64_t ticks);
  void flush_count();

  Bus &bus_;
  CpuState state_{};
  std::array<std::uint64_t, 32> control_{};
  std::array<TlbEntry, 32> tlb_{};
  std::array<CacheLine, 512> icache_{};
  std::array<CacheLine, 512> dcache_{};
  std::uint64_t instruction_cache_generation_ = 1;
  std::uint64_t control_latch_ = 0;
  std::uint64_t cop2_latch_ = 0;
  std::uint64_t count_ticks_ = 0;
  std::uint64_t count_clock_ = 0;
  std::uint64_t pipeline_pc_ = 0;
  std::uint64_t next_pc_ = 0;
  RandomGenerator entropy_;
  RandomGenerator &random_;
  bool delay_slot_ = false;
  bool next_delay_slot_ = false;
  bool block_exit_ = false;
  bool next_block_exit_ = false;
  bool llbit_ = false;
  bool nmi_pending_ = false;
  bool native_memory_order_ = false;
  std::function<void()> synchronize_;
  std::unique_ptr<CpuCompiler> compiler_;
};

} // namespace cupid::n64
