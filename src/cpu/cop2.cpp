#include "cupid/cpu.hpp"

namespace cupid {

void Cpu::execute_cop2(u32 instruction) {
    if (!require_coprocessor(2))
        return;
    const unsigned rt = (instruction >> 16) & 31U;
    switch ((instruction >> 21) & 31U) {
    case 0:
    case 2:
        gpr[rt] = sign_extend32(static_cast<u32>(cop2_latch));
        return;
    case 1:
        gpr[rt] = cop2_latch;
        return;
    case 4:
    case 5:
    case 6:
        cop2_latch = gpr[rt];
        return;
    default:
        raise_exception(Exception::ReservedInstruction, 2);
        return;
    }
}

} // namespace cupid
