#pragma once

#include "cupid/rsp/pipeline.hpp"
#include "cupid/types.hpp"

#include <array>

namespace cupid {

class System;

class Rsp {
  public:
    explicit Rsp(System& system);

    void reset();
    void tick(u64 rcp_cycles);
    void step();
    [[nodiscard]] bool running() const {
        return !halted_;
    }

    [[nodiscard]] u32 read_register(u32 byte_offset);
    void write_register(u32 byte_offset, u32 value);
    void write_pc(u32 value);

    std::array<u8, 8192> memory{};
    u32 pc{};

  private:
    friend class System;
    [[nodiscard]] u64 next_dma_event() const;
    [[nodiscard]] bool local_execution_ready() const;
    [[nodiscard]] u64 run_local(u64 maximum_cycles);
    [[nodiscard]] bool step_local();
    void execute_group();

    struct Vector {
        std::array<u8, 16> byte{};
    };

    struct Accumulator {
        std::array<u16, 8> low{};
        std::array<u16, 8> middle{};
        std::array<u16, 8> high{};
    };

    struct DmaTransfer {
        u16 sp_address{};
        u32 dram_address{};
        u16 length{};
        u8 count{};
        u16 skip{};
        bool to_sp{};
    };

    System& system_;
    std::array<u32, 32> gpr_{};
    std::array<Vector, 32> vr_{};
    Accumulator accumulator_{};

    u8 vcol_{};
    u8 vcoh_{};
    u8 vccl_{};
    u8 vcch_{};
    u8 vce_{};
    s16 div_input_{};
    s16 div_output_{};
    bool div_input_high_{};

    bool halted_{true};
    bool broke_{};
    bool single_step_{};
    bool interrupt_on_break_{};
    std::array<bool, 8> signal_{};
    bool semaphore_{};

    DmaTransfer dma_current_{};
    DmaTransfer dma_pending_{};
    bool dma_busy_{};
    bool dma_full_{};
    u64 dma_cycles_until_row_{};

    u32 next_pc_{4};
    u32 pc_shadow_{};
    u32 current_pc_{};
    bool branch_pending_{};
    RspPipeline pipeline_{};

    [[nodiscard]] u8 dmem_read8(u32 address) const;
    [[nodiscard]] u16 dmem_read16(u32 address) const;
    [[nodiscard]] u32 dmem_read32(u32 address) const;
    void dmem_write8(u32 address, u8 value);
    void dmem_write16(u32 address, u16 value);
    void dmem_write32(u32 address, u32 value);

    [[nodiscard]] u32 fetch_instruction(u32 address) const;
    void execute_scalar(u32 instruction);
    void execute_special(u32 instruction);
    void execute_regimm(u32 instruction);
    void execute_cop0(u32 instruction);
    void execute_cop2(u32 instruction);
    void execute_vector_op(u32 instruction);
    [[nodiscard]] bool execute_vector_op_sse2(u32 instruction);
    void execute_vector_load(u32 instruction);
    void execute_vector_store(u32 instruction);

    void take_branch(u32 target);
    void write_gpr(unsigned index, u32 value);

    void start_dma(bool to_sp, u32 value);
    void promote_dma();
    void tick_dma(u64 rcp_cycles);
    void transfer_dma_row();

    [[nodiscard]] static u16 vec_u16(const Vector& vector, unsigned lane);
    [[nodiscard]] static s16 vec_s16(const Vector& vector, unsigned lane);
    static void vec_set_u16(Vector& vector, unsigned lane, u16 value);
    static void vec_set_s16(Vector& vector, unsigned lane, s16 value);

    [[nodiscard]] static s16 clamp_s16(s64 value);
    [[nodiscard]] static s64 wrap_accumulator(s64 value);
    [[nodiscard]] s64 read_accumulator(unsigned lane) const;
    void set_accumulator(unsigned lane, s64 value);
    [[nodiscard]] u16 acc_low(unsigned lane) const;
    [[nodiscard]] u16 acc_mid(unsigned lane) const;
    [[nodiscard]] u16 acc_high(unsigned lane) const;
    [[nodiscard]] s16 acc_mid_s(unsigned lane) const;
    [[nodiscard]] s16 acc_high_s(unsigned lane) const;
    void set_acc_low(unsigned lane, u16 value);
    void set_acc_mid(unsigned lane, u16 value);
    void set_acc_high(unsigned lane, u16 value);
    [[nodiscard]] u16 saturate_accumulator(unsigned lane, bool middle_slice, u16 negative,
                                           u16 positive) const;

    [[nodiscard]] bool flag(u8 mask, unsigned lane) const;
    static void set_flag(u8& mask, unsigned lane, bool value);

    [[nodiscard]] static u32 reciprocal(u32 value);
    [[nodiscard]] static u32 reciprocal_sqrt(u32 value);
};

} // namespace cupid
