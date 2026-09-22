#pragma once

#include "cupid/fpu.hpp"
#include "cupid/types.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

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

namespace cpu_timing {
[[nodiscard]] u64 cache_miss_sclock_extra(u64 pending_cpu_cycles, u64 system_fraction);
}

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
    void request_nmi();
    void step();
    unsigned run_slice(unsigned maximum_steps, u64 maximum_cycles);
    [[nodiscard]] u64 batched_idle_instructions() const {
        return batched_idle_instructions_;
    }
    [[nodiscard]] u64 batched_cached_instructions() const {
        return batched_cached_instructions_;
    }
    // Standalone instruction and memory helpers are untimed; step advances the hardware clocks.
    void execute(u32 instruction);
    void set_pc(u64 address);

    [[nodiscard]] u32 status() const {
        return static_cast<u32>(cp0[12]);
    }
    [[nodiscard]] bool kernel_mode() const {
        return (status() & 6U) != 0 || (status() & 0x18U) == 0;
    }
    [[nodiscard]] bool wide_addressing() const {
        const unsigned mode = kernel_mode() ? 0U : std::min((status() >> 3) & 3U, 2U);
        return (status() & (0x80U >> mode)) != 0;
    }
    [[nodiscard]] bool wide_instructions() const {
        return kernel_mode() || wide_addressing();
    }
    bool require_coprocessor(unsigned coprocessor);
    void raise_exception(Exception exception, unsigned coprocessor = 0, bool refill = false,
                         bool instruction_fetch = false);
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
    friend class System;
    // Registers and pipeline state stay together so dispatch does not reload them
    // from behind the instruction and data caches.
    System& system_;
    u64 instruction_cycles_{1};
    u64 synchronized_instruction_cycles_{};
    u64 following_pc_{};
    u64 count_write_hold_{};
    u64 cop0_latch_{};
    u64 batched_idle_instructions_{};
    u64 batched_cached_instructions_{};
    struct FetchedInstruction {
        u64 address{};
        u32 instruction{};
        bool valid{};
    };
    FetchedInstruction fetched_instruction_{};
    u32 random_{31};
    u32 random_wired_{};
    unsigned software_interrupt_delay_{};
    unsigned pending_load_register_{};
    unsigned pending_fpu_register_{32};
    bool executing_step_{};
    bool nmi_pending_{};
    bool in_delay_slot_{};
    bool following_delay_slot_{};
    bool annul_next_{};
    bool redirected_{};
    bool count_half_{};
    bool speculative_fetch_{};

  public:
    std::array<TlbEntry, 32> tlb{};
    std::array<CacheLine<16>, 512> data_cache{};
    std::array<CacheLine<32>, 512> instruction_cache{};
    Fpu fpu;

  private:
    struct MemoryWrite {
        u64 address{};
        unsigned width{};
        u64 value{};
    };
    struct BufferedWrite {
        std::array<MemoryWrite, 3> transfers{};
        unsigned count{};
        u64 remaining{};
    };
    std::array<BufferedWrite, 4> write_buffer_{};
    unsigned write_buffer_head_{};
    unsigned write_buffer_count_{};
    // step() can issue the next-PC fetch plus one store-prefetch or exception-decode fetch.
    std::array<u32, 2> speculative_refill_bases_{};
    unsigned speculative_refill_count_{};
    struct WiredWrite {
        u32 value{};
        u64 instruction{};
    };
    std::array<WiredWrite, 2> wired_writes_{};

    enum class CachedKind : u8 { Unsupported, Private, Load, Store };
    enum class CachedDirect : u8 {
        None,
        Sll,
        Srl,
        Sra,
        Sllv,
        Srlv,
        Srav,
        Jr,
        Jalr,
        Sync,
        Mfhi,
        Mthi,
        Mflo,
        Mtlo,
        Dsllv,
        Dsrlv,
        Dsrav,
        Addu,
        Subu,
        And,
        Or,
        Xor,
        Nor,
        Slt,
        Sltu,
        Daddu,
        Dsubu,
        Dsll,
        Dsrl,
        Dsra,
        Dsll32,
        Dsrl32,
        Dsra32,
        Bltz,
        Bgez,
        Bltzal,
        Bgezal,
        J,
        Jal,
        Beq,
        Bne,
        Blez,
        Bgtz,
        Addiu,
        Slti,
        Sltiu,
        Andi,
        Ori,
        Xori,
        Lui,
        Daddiu,
    };
    struct CachedDecode {
        u32 word{};
        u32 integer_reads{};
        CachedKind kind{};
        CachedDirect direct{};
        u8 rs{};
        u8 rt{};
        u8 rd{};
        u8 sa{};
        u8 load_target{32};
        u8 memory_width : 4 {};
        u8 signed_load : 1 {};
        u8 valid : 1 {};
    };
    std::vector<CachedDecode> cached_decode_ = std::vector<CachedDecode>(4096);
    struct CachedLinePlan {
        std::array<u8, 32> image{};
        u32 tag{};
        bool valid{};
    };
    std::vector<CachedLinePlan> cached_line_plans_ = std::vector<CachedLinePlan>(512);

    void begin_instruction_timing(u32 instruction);
    void finish_instruction_timing(u32 instruction);
    unsigned sample_exception_coprocessor();
    bool fetch_instruction(u64& instruction);
    void prefetch_instruction(u64 address, bool latch);
    // A kseg0 instruction-cache hit can be latched without the miss-path guard.
    bool latch_cached_instruction(u64 address);

    void execute_special(u32 instruction);
    void accept_nmi();
    void execute_regimm(u32 instruction);
    void execute_cop0(u32 instruction);
    void write_cop0_instruction(unsigned index, u64 value);
    void execute_cop2(u32 instruction);
    void advance_clock_counters(u64 elapsed);
    void advance_batched_instruction_counters(unsigned instructions);
    void update_clocks(u64 elapsed);
    [[nodiscard]] static bool branches_to_self(u32 instruction, u64 address);
    unsigned batch_idle_loop(unsigned maximum_steps, u64 maximum_cycles);
    unsigned batch_cached_private(unsigned maximum_steps, u64 maximum_cycles);
    [[nodiscard]] CachedDecode decode_cached_instruction(u32 instruction) const;
    void execute_cached_direct(const CachedDecode& decoded);
    void execute_cached_memory(const CachedDecode& decoded, CacheLine<16>& line, unsigned offset);
    void update_interrupt_inputs();
    void synchronize();
    void complete_speculative_refills();
    void complete_multicycle_instruction(u64 extra_cycles);
    [[nodiscard]] u64 cache_miss_sclock_extra() const;
    [[nodiscard]] u64 rdram_refresh_delay(u32 physical) const;
    void buffer_write(u32 physical, unsigned width, u64 value);
    void buffer_writes(std::span<const MemoryWrite> transfers);
    void drain_write_buffer();
    void tick_write_buffer(u64 rcp_cycles);
    [[nodiscard]] u64 next_buffered_write() const;
    [[nodiscard]] bool little_endian() const {
        const bool reverse = !kernel_mode() && (status() & 0x18U) >= 0x10U && (status() & 0x02000000U) != 0;
        return ((cp0[16] & 0x8000U) == 0) != reverse;
    }
    void address_exception(u64 address, Access access);
    void tlb_exception(u64 address, Access access, bool refill, bool modification);
    bool writeback(CacheLine<16>& line, unsigned index);
    bool fill_data_cache(CacheLine<16>& line, u32 physical, unsigned index);
    bool load_partial(u64 address, unsigned width, bool left, unsigned target);
    bool store_partial(u64 address, unsigned width, bool left, u64 value);
    bool prepare_write(u64 address, unsigned width, bool check_alignment, u32& physical, bool& cached);
    bool write_partial(std::span<const MemoryWrite> transfers);
};

} // namespace cupid
