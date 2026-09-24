#pragma once

#include "cupid/rsp/memory.hpp"
#include "cupid/rsp/pipeline.hpp"
#include "cupid/types.hpp"

#include <array>
#include <bit>
#include <memory>

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

    RspMemory memory{};
    u32 pc{};

  private:
    friend class System;
    [[nodiscard]] u64 next_dma_event() const;
    [[nodiscard]] bool local_execution_ready() const;
    [[nodiscard]] u64 run_local(u64 maximum_cycles);
    [[nodiscard]] u64 execute_local(u64 maximum_cycles);
    void execute_group();

    // Local instructions may run before the shared clock reaches them. The
    // checkpoint lets an observer rewind to the shared clock and replay the
    // cycles that clock has already passed.
    [[nodiscard]] u64 lead() const {
        return lead_cycles_;
    }
    void run_ahead(u64 maximum_cycles);
    [[nodiscard]] u64 consume_lead(u64 rcp_cycles);
    void rewind_lead();

    struct Vector {
        std::array<u16, 8> lane{};
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

    struct ScalarOperands {
        u8 rs{};
        u8 rt{};
        u8 rd{};
        u8 sa{};
        s16 immediate{};
    };
    static_assert(sizeof(ScalarOperands) == 6);

    struct LocalBlock {
        struct Instruction {
            u32 word{};
            RspPipeline::Operation operation{};
            ScalarOperands operands{};
        };
        static_assert(sizeof(Instruction) == 12);
        std::array<Instruction, 16> instructions{};
        std::array<RspPipeline::Stage, 3> incoming{};
        RspPipeline::Snapshot outgoing{};
        u64 revision{};
        u32 next_pc{};
        unsigned count{};
        unsigned cycles{};
        bool single_issue{};
        bool valid{};
    };
    struct LocalBlocks {
        std::unique_ptr<std::array<LocalBlock, 1024>> ptr;
        LocalBlocks() = default;
        ~LocalBlocks() = default;
        LocalBlocks(const LocalBlocks& other)
            : ptr(other.ptr ? std::make_unique<std::array<LocalBlock, 1024>>(*other.ptr) : nullptr) {}
        LocalBlocks& operator=(const LocalBlocks& other) {
            if (this != &other)
                ptr = other.ptr ? std::make_unique<std::array<LocalBlock, 1024>>(*other.ptr) : nullptr;
            return *this;
        }
        LocalBlocks(LocalBlocks&&) noexcept = default;
        LocalBlocks& operator=(LocalBlocks&&) noexcept = default;

        [[nodiscard]] std::array<LocalBlock, 1024>& operator*() {
            return *ptr;
        }
        [[nodiscard]] const std::array<LocalBlock, 1024>& operator*() const {
            return *ptr;
        }
        [[nodiscard]] std::array<LocalBlock, 1024>* operator->() {
            return ptr.get();
        }
        [[nodiscard]] const std::array<LocalBlock, 1024>* operator->() const {
            return ptr.get();
        }
        [[nodiscard]] explicit operator bool() const {
            return static_cast<bool>(ptr);
        }
        void reset(std::unique_ptr<std::array<LocalBlock, 1024>> p = nullptr) {
            ptr = std::move(p);
        }
    };
    LocalBlocks local_blocks_;
    [[nodiscard]] u64 execute_local_block(u64 revision, u64 maximum_cycles);
    void prepare_local_block(LocalBlock& block, u64 revision);

    // Local groups change only these fields and DMEM.
    struct LocalState {
        std::array<u32, 32> gpr{};
        std::array<Vector, 32> vr{};
        Accumulator accumulator{};
        u8 vcol{};
        u8 vcoh{};
        u8 vccl{};
        u8 vcch{};
        u8 vce{};
        s16 div_input{};
        s16 div_output{};
        bool div_input_high{};
        u64 dma_cycles_until_row{};
        u32 pc{};
        u32 next_pc{};
        u32 pc_shadow{};
        u32 current_pc{};
        bool branch_pending{};
        DmaTransfer dma_pending{};
        RspPipeline::Snapshot pipeline{};
    };
    LocalState checkpoint_{};
    u64 lead_cycles_{};
    u64 lead_elapsed_{};
    // DMEM keeps the checkpoint's contents only for 16-byte blocks the lead writes.
    std::array<std::array<u8, 16>, 256> saved_dmem_{};
    std::array<u64, 4> saved_dmem_blocks_{};
    bool tracking_dmem_{};
    [[nodiscard]] bool can_issue_local();
    void save_dmem(u32 address, u32 bytes) {
        if (!tracking_dmem_)
            return;
        save_dmem_block((address & 0x0fffU) >> 4U);
        save_dmem_block(((address + bytes - 1U) & 0x0fffU) >> 4U);
    }
    void save_dmem_block(u32 block);

    [[nodiscard]] static constexpr u32 mask_pc(u32 value) noexcept {
        return value & 0x0ffcU;
    }

    [[nodiscard]] u8 dmem_read8(u32 address) const noexcept {
        return memory.internal_read(address & 0x0fffU);
    }
    [[nodiscard]] u16 dmem_read16(u32 address) const noexcept {
        const u32 offset = address & 0x0fffU;
        if (offset + 2U <= 0x1000U)
            return read_be16(memory.internal_data() + offset);
        return static_cast<u16>((static_cast<u16>(dmem_read8(address)) << 8) | dmem_read8(address + 1));
    }
    [[nodiscard]] u32 dmem_read32(u32 address) const noexcept {
        const u32 offset = address & 0x0fffU;
        if (offset + 4U <= 0x1000U)
            return read_be32(memory.internal_data() + offset);
        return (static_cast<u32>(dmem_read8(address)) << 24) |
               (static_cast<u32>(dmem_read8(address + 1)) << 16) |
               (static_cast<u32>(dmem_read8(address + 2)) << 8) | static_cast<u32>(dmem_read8(address + 3));
    }
    void dmem_write8(u32 address, u8 value) noexcept {
        save_dmem(address, 1);
        memory.internal_write(address & 0x0fffU, value);
    }
    void dmem_write16(u32 address, u16 value) noexcept {
        const u32 offset = address & 0x0fffU;
        if (offset + 2U <= 0x1000U) {
            save_dmem(offset, 2);
            write_be16(memory.internal_data() + offset, value);
            return;
        }
        dmem_write8(address, static_cast<u8>(value >> 8));
        dmem_write8(address + 1, static_cast<u8>(value));
    }
    void dmem_write32(u32 address, u32 value) noexcept {
        const u32 offset = address & 0x0fffU;
        if (offset + 4U <= 0x1000U) {
            save_dmem(offset, 4);
            write_be32(memory.internal_data() + offset, value);
            return;
        }
        dmem_write8(address, static_cast<u8>(value >> 24));
        dmem_write8(address + 1, static_cast<u8>(value >> 16));
        dmem_write8(address + 2, static_cast<u8>(value >> 8));
        dmem_write8(address + 3, static_cast<u8>(value));
    }

    [[nodiscard]] u32 fetch_instruction(u32 address) const noexcept {
        return read_be32(memory.internal_data() + (0x1000U | mask_pc(address)));
    }
    [[nodiscard]] static ScalarOperands decode_scalar_operands(u32 instruction);
    void execute_decoded(u32 instruction, RspPipeline::Operation operation);
    void execute_decoded(u32 instruction, RspPipeline::Operation operation, const ScalarOperands& operands);
    void execute_cop0(u32 instruction);
    void execute_cop2(u32 instruction);
    void execute_vector_op(u32 instruction);
    void execute_vector_op_scalar(u32 instruction);
    [[nodiscard]] bool execute_vector_op_sse2(u32 instruction);
    void execute_vector_load(u32 instruction);
    void execute_vector_store(u32 instruction);

    void take_branch(u32 target) noexcept {
        next_pc_ = mask_pc(target);
        branch_pending_ = true;
    }
    void write_gpr(unsigned index, u32 value) noexcept {
        if (index != 0) {
            gpr_[index & 31] = value;
        }
    }

    void start_dma(bool to_sp, u32 value);
    void promote_dma();
    void tick_dma(u64 rcp_cycles);
    void transfer_dma_row();

    [[nodiscard]] static u16 vec_u16(const Vector& vector, unsigned lane) noexcept {
        return vector.lane[lane & 7U];
    }
    [[nodiscard]] static s16 vec_s16(const Vector& vector, unsigned lane) noexcept {
        return std::bit_cast<s16>(vec_u16(vector, lane));
    }
    static void vec_set_u16(Vector& vector, unsigned lane, u16 value) noexcept {
        vector.lane[lane & 7U] = value;
    }
    static void vec_set_s16(Vector& vector, unsigned lane, s16 value) noexcept {
        vec_set_u16(vector, lane, std::bit_cast<u16>(value));
    }
    [[nodiscard]] static u8 vec_byte(const Vector& vector, unsigned byte) noexcept {
        const unsigned index = byte & 15U;
        const u16 value = vector.lane[index >> 1U];
        return (index & 1U) != 0U ? static_cast<u8>(value) : static_cast<u8>(value >> 8U);
    }
    static void vec_set_byte(Vector& vector, unsigned byte, u8 value) noexcept {
        const unsigned index = byte & 15U;
        u16& lane = vector.lane[index >> 1U];
        if ((index & 1U) == 0U)
            lane = static_cast<u16>((lane & 0x00ffU) | (static_cast<u32>(value) << 8U));
        else
            lane = static_cast<u16>((lane & 0xff00U) | value);
    }
    void load_plain_vector(Vector& target, unsigned element, u32 address, unsigned width);
    void store_plain_vector(u32 address, const Vector& source, unsigned element, unsigned count);

    [[nodiscard]] static s16 clamp_s16(s64 value);
    [[nodiscard]] static s64 wrap_accumulator(s64 value);
    [[nodiscard]] s64 read_accumulator(unsigned lane) const;
    void set_accumulator(unsigned lane, s64 value);
    [[nodiscard]] u16 acc_low(unsigned lane) const noexcept {
        return accumulator_.low[lane & 7U];
    }
    [[nodiscard]] u16 acc_mid(unsigned lane) const noexcept {
        return accumulator_.middle[lane & 7U];
    }
    [[nodiscard]] u16 acc_high(unsigned lane) const noexcept {
        return accumulator_.high[lane & 7U];
    }
    [[nodiscard]] s16 acc_mid_s(unsigned lane) const noexcept {
        return std::bit_cast<s16>(acc_mid(lane));
    }
    [[nodiscard]] s16 acc_high_s(unsigned lane) const noexcept {
        return std::bit_cast<s16>(acc_high(lane));
    }
    void set_acc_low(unsigned lane, u16 value) noexcept {
        accumulator_.low[lane & 7U] = value;
    }
    void set_acc_mid(unsigned lane, u16 value) noexcept {
        accumulator_.middle[lane & 7U] = value;
    }
    void set_acc_high(unsigned lane, u16 value) noexcept {
        accumulator_.high[lane & 7U] = value;
    }
    [[nodiscard]] u16 saturate_accumulator(unsigned lane, bool middle_slice, u16 negative,
                                           u16 positive) const;

    [[nodiscard]] bool flag(u8 mask, unsigned lane) const noexcept {
        return ((mask >> (lane & 7)) & 1) != 0;
    }
    static void set_flag(u8& mask, unsigned lane, bool value) noexcept {
        const u8 bit = static_cast<u8>(1u << (lane & 7));
        mask = value ? static_cast<u8>(mask | bit) : static_cast<u8>(mask & ~bit);
    }

    [[nodiscard]] static u32 reciprocal(u32 value);
    [[nodiscard]] static u32 reciprocal_sqrt(u32 value);
};

} // namespace cupid
