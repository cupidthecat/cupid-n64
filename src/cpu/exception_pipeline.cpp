#include "cupid/system.hpp"

namespace cupid {

unsigned Cpu::sample_exception_coprocessor() {
    // The younger instruction reaches decode before the FPU exception redirects the pipeline.
    // Its fetch faults cannot replace the older exception or update translation fault registers.
    const bool was_frozen = frozen;
    sampling_exception_decode_ = true;
    u64 instruction = 0;
    const bool fetched = read_memory(next_pc, 4, instruction, true);
    sampling_exception_decode_ = false;
    frozen = was_frozen;
    if (!fetched)
        return 0;
    const unsigned opcode = static_cast<u32>(instruction) >> 26;
    return (opcode & 0x3cU) == 0x10U ? opcode & 3U : 0U;
}

} // namespace cupid
