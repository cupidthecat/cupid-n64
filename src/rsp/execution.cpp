#include "cupid/rsp.hpp"

#include "cupid/system.hpp"

#include <bit>

namespace cupid {
namespace {

constexpr u32 mask_pc(u32 value) {
    return value & 0x0ffcU;
}

constexpr s32 as_s32(u32 value) {
    return std::bit_cast<s32>(value);
}

constexpr u32 as_u32(s32 value) {
    return std::bit_cast<u32>(value);
}

} // namespace

void Rsp::execute_decoded(u32 instruction, RspPipeline::Operation operation) {
    using Operation = RspPipeline::Operation;

    const unsigned rs = (instruction >> 21U) & 31U;
    const unsigned rt = (instruction >> 16U) & 31U;
    const unsigned rd = (instruction >> 11U) & 31U;
    const unsigned sa = (instruction >> 6U) & 31U;
    const s16 imm = std::bit_cast<s16>(static_cast<u16>(instruction));
    const auto branch_target = [&] { return current_pc_ + 4 + static_cast<u32>(static_cast<s32>(imm) * 4); };

    switch (operation) {
    case Operation::None:
        return;
    case Operation::Sll:
        write_gpr(rd, gpr_[rt] << sa);
        return;
    case Operation::Srl:
        write_gpr(rd, gpr_[rt] >> sa);
        return;
    case Operation::Sra:
        write_gpr(rd, as_u32(as_s32(gpr_[rt]) >> sa));
        return;
    case Operation::Sllv:
        write_gpr(rd, gpr_[rt] << (gpr_[rs] & 31U));
        return;
    case Operation::Srlv:
        write_gpr(rd, gpr_[rt] >> (gpr_[rs] & 31U));
        return;
    case Operation::Srav:
        write_gpr(rd, as_u32(as_s32(gpr_[rt]) >> (gpr_[rs] & 31U)));
        return;
    case Operation::Jr:
        take_branch(gpr_[rs]);
        return;
    case Operation::Jalr: {
        const u32 target = gpr_[rs];
        write_gpr(rd, mask_pc(current_pc_ + 8));
        take_branch(target);
        return;
    }
    case Operation::Break:
        halted_ = true;
        broke_ = true;
        if (interrupt_on_break_)
            system_.bus.set_interrupt(0, true);
        return;
    case Operation::Addu:
        write_gpr(rd, gpr_[rs] + gpr_[rt]);
        return;
    case Operation::Subu:
        write_gpr(rd, gpr_[rs] - gpr_[rt]);
        return;
    case Operation::And:
        write_gpr(rd, gpr_[rs] & gpr_[rt]);
        return;
    case Operation::Or:
        write_gpr(rd, gpr_[rs] | gpr_[rt]);
        return;
    case Operation::Xor:
        write_gpr(rd, gpr_[rs] ^ gpr_[rt]);
        return;
    case Operation::Nor:
        write_gpr(rd, ~(gpr_[rs] | gpr_[rt]));
        return;
    case Operation::Slt:
        write_gpr(rd, as_s32(gpr_[rs]) < as_s32(gpr_[rt]));
        return;
    case Operation::Sltu:
        write_gpr(rd, gpr_[rs] < gpr_[rt]);
        return;
    case Operation::Bltz:
        if (as_s32(gpr_[rs]) < 0)
            take_branch(branch_target());
        return;
    case Operation::Bgez:
        if (as_s32(gpr_[rs]) >= 0)
            take_branch(branch_target());
        return;
    case Operation::Bltzal: {
        const bool negative = as_s32(gpr_[rs]) < 0;
        write_gpr(31, mask_pc(current_pc_ + 8));
        if (negative)
            take_branch(branch_target());
        return;
    }
    case Operation::Bgezal: {
        const bool negative = as_s32(gpr_[rs]) < 0;
        write_gpr(31, mask_pc(current_pc_ + 8));
        if (!negative)
            take_branch(branch_target());
        return;
    }
    case Operation::J:
        take_branch((instruction & 0x03ff'ffffU) << 2U);
        return;
    case Operation::Jal:
        write_gpr(31, mask_pc(current_pc_ + 8));
        take_branch((instruction & 0x03ff'ffffU) << 2U);
        return;
    case Operation::Beq:
        if (gpr_[rs] == gpr_[rt])
            take_branch(branch_target());
        return;
    case Operation::Bne:
        if (gpr_[rs] != gpr_[rt])
            take_branch(branch_target());
        return;
    case Operation::Blez:
        if (as_s32(gpr_[rs]) <= 0)
            take_branch(branch_target());
        return;
    case Operation::Bgtz:
        if (as_s32(gpr_[rs]) > 0)
            take_branch(branch_target());
        return;
    case Operation::Addiu:
        write_gpr(rt, gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)));
        return;
    case Operation::Slti:
        write_gpr(rt, as_s32(gpr_[rs]) < static_cast<s32>(imm));
        return;
    case Operation::Sltiu:
        write_gpr(rt, gpr_[rs] < static_cast<u32>(static_cast<s32>(imm)));
        return;
    case Operation::Andi:
        write_gpr(rt, gpr_[rs] & static_cast<u16>(imm));
        return;
    case Operation::Ori:
        write_gpr(rt, gpr_[rs] | static_cast<u16>(imm));
        return;
    case Operation::Xori:
        write_gpr(rt, gpr_[rs] ^ static_cast<u16>(imm));
        return;
    case Operation::Lui:
        write_gpr(rt, static_cast<u32>(static_cast<u16>(imm)) << 16U);
        return;
    case Operation::Cop0:
        execute_cop0(instruction);
        return;
    case Operation::Cop2:
        execute_cop2(instruction);
        return;
    case Operation::Lb:
        write_gpr(rt, as_u32(static_cast<s32>(
                          static_cast<s8>(dmem_read8(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)))))));
        return;
    case Operation::Lh:
        write_gpr(rt, as_u32(static_cast<s32>(static_cast<s16>(
                          dmem_read16(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)))))));
        return;
    case Operation::Lw:
        write_gpr(rt, dmem_read32(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm))));
        return;
    case Operation::Lbu:
        write_gpr(rt, dmem_read8(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm))));
        return;
    case Operation::Lhu:
        write_gpr(rt, dmem_read16(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm))));
        return;
    case Operation::Sb:
        dmem_write8(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)), static_cast<u8>(gpr_[rt]));
        return;
    case Operation::Sh:
        dmem_write16(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)), static_cast<u16>(gpr_[rt]));
        return;
    case Operation::Sw:
        dmem_write32(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)), gpr_[rt]);
        return;
    case Operation::VectorLoad:
        execute_vector_load(instruction);
        return;
    case Operation::VectorStore:
        execute_vector_store(instruction);
        return;
    case Operation::ReservedSpecial:
        // Undefined SPECIAL encodings expose an otherwise unwired shift behavior.
        write_gpr(rd, gpr_[rs] >> (gpr_[rs] & 31U));
        return;
    }
}

} // namespace cupid
