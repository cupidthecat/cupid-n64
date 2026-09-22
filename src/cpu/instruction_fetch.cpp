#include "cupid/system.hpp"

namespace cupid {

bool Cpu::fetch_instruction(u64& instruction) {
    if (fetched_instruction_.valid && fetched_instruction_.address == pc) {
        instruction = fetched_instruction_.instruction;
        fetched_instruction_.valid = false;
        return true;
    }
    fetched_instruction_.valid = false;
    return read_memory(pc, 4, instruction, true);
}

void Cpu::prefetch_instruction(u64 address, bool latch) {
    if (latch)
        fetched_instruction_.valid = false;
    if ((address & 3U) != 0)
        return;
    struct FetchGuard {
        bool& speculative;
        bool& frozen;
        bool previous_frozen;
        ~FetchGuard() {
            speculative = false;
            frozen = previous_frozen;
        }
    } guard{speculative_fetch_, frozen, frozen};
    speculative_fetch_ = true;
    u32 physical = 0;
    bool cached = false;
    if (!translate(address, Access::Execute, physical, cached) || !cached)
        return;
    if (little_endian())
        physical ^= 4U;
    const auto& line = instruction_cache[(address >> 5) & 511U];
    const bool hit = line.valid && line.tag == (physical & 0xfffff000U);
    if (hit) {
        // A hit has already reached the register-fetch latch.
        if (latch) {
            fetched_instruction_.address = address;
            fetched_instruction_.instruction = read_be32(line.data.data() + (physical & 28U));
            fetched_instruction_.valid = true;
        }
        return;
    }
    // A miss refills the cache, but the instruction must still be read after an
    // older cache operation finishes.
    u64 instruction = 0;
    static_cast<void>(read_memory(address, 4, instruction, true));
}

} // namespace cupid
