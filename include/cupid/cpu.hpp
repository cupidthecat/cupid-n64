#pragma once

#include "cupid/fpu.hpp"
#include "cupid/types.hpp"

#include <array>

namespace cupid {

class System;

enum class Exception : unsigned {
    Interrupt = 0,
    TlbModification = 1,
    TlbLoad = 2,
    TlbStore = 3,
    AddressLoad = 4,
    AddressStore = 5,
    BusInstruction = 6,
    BusData = 7,
    Syscall = 8,
    Breakpoint = 9,
    ReservedInstruction = 10,
    CoprocessorUnusable = 11,
    Overflow = 12,
    Trap = 13,
    FloatingPoint = 15,
    Watch = 23,
};

enum class Access { Read, Write, Execute };

struct TlbEntry {
    u64 entry_hi{};
    std::array<u32, 2> entry_lo{};
    u32 page_mask{};
    bool global{};
};

template <unsigned Size> struct CacheLine {
    std::array<u8, Size> data{};
    u32 tag{};
    bool valid{};
    bool dirty{};
};

class Cpu {
  public:
    explicit Cpu(System& system);
    void reset();
    void step();
    void execute(u32 instruction);
    void set_pc(u64 address);

    [[nodiscard]] u32 status() const {
        return static_cast<u32>(cp0[12]);
    }
    [[nodiscard]] bool kernel_mode() const;
    [[nodiscard]] bool wide_addressing() const;
    [[nodiscard]] bool wide_instructions() const;
    bool require_coprocessor(unsigned coprocessor);
    void raise_exception(Exception exception, unsigned coprocessor = 0, bool refill = false);
    void branch(bool condition, u64 target, bool likely = false);
    void add_cycles(u64 amount) {
        instruction_cycles_ += amount;
    }

    bool translate(u64 address, Access access, u32& physical, bool& cached);
    bool read_memory(u64 address, unsigned width, u64& value, bool instruction = false);
    bool write_memory(u64 address, unsigned width, u64 value, bool check_alignment = true);
    u64 read_cop0(unsigned index);
    void write_cop0(unsigned index, u64 value);
    void cache_operation(unsigned operation, u64 address);
    void tlb_read();
    void tlb_write(unsigned index);
    void tlb_probe();

    std::array<u64, 32> gpr{};
    std::array<u64, 32> cp0{};
    std::array<TlbEntry, 32> tlb{};
    std::array<CacheLine<16>, 512> data_cache{};
    std::array<CacheLine<32>, 512> instruction_cache{};
    Fpu fpu;
    u64 pc{};
    u64 next_pc{};
    u64 hi{};
    u64 lo{};
    u64 cycles{};
    u64 instruction_count{};
    bool exception_pending{};
    bool frozen{};
    bool linked{};
    u64 cop2_latch{};

  private:
    System& system_;
    u64 cop0_latch_{};
    u64 instruction_cycles_{1};
    u64 following_pc_{};
    bool in_delay_slot_{};
    bool following_delay_slot_{};
    bool annul_next_{};
    bool redirected_{};
    bool count_half_{};
    u64 count_write_hold_{};
    u32 random_{31};
    unsigned pending_load_register_{};

    void begin_instruction_timing(u32 instruction);
    void finish_instruction_timing(u32 instruction);

    void execute_special(u32 instruction);
    void execute_regimm(u32 instruction);
    void execute_cop0(u32 instruction);
    void execute_cop2(u32 instruction);
    void update_clocks(u64 elapsed);
    [[nodiscard]] bool little_endian() const;
    void address_exception(u64 address, Access access);
    void tlb_exception(u64 address, Access access, bool refill, bool modification);
    bool writeback(CacheLine<16>& line, unsigned index);
    bool fill_data_cache(CacheLine<16>& line, u32 physical, unsigned index);
    bool load_partial(u64 address, unsigned width, bool left, unsigned target);
    bool store_partial(u64 address, unsigned width, bool left, u64 value);
};

} // namespace cupid
