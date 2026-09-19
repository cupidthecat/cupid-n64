#include "cupid/system.hpp"

namespace cupid {

bool Cpu::fetch_instruction(u64& instruction) {
    if (fetched_instruction_.valid && fetched_instruction_.address == pc) {
        instruction = fetched_instruction_.instruction;
        fetched_instruction_ = {};
        return true;
    }
    fetched_instruction_ = {};
    return read_memory(pc, 4, instruction, true);
}

void Cpu::prefetch_instruction(u64 address, bool latch) {
    if (latch)
        fetched_instruction_ = {};
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
    u64 instruction = 0;
    if (!read_memory(address, 4, instruction, true))
        return;
    // A hit has already reached the register-fetch latch. A miss refills the cache,
    // but the instruction must still be read after an older cache operation finishes.
    if (hit && latch)
        fetched_instruction_ = {address, static_cast<u32>(instruction), true};
}

} // namespace cupid
