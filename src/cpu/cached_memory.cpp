#include "cupid/cpu.hpp"

#include <cassert>

namespace cupid {

void Cpu::execute_cached_memory(const CachedDecode& decoded, CacheLine<16>& line, unsigned offset) {
    // The current instruction's preflight proved alignment, kernel kseg0,
    // big-endian mode, and a matching live data-cache tag. No device can access
    // these cached bytes before the slice's next event boundary.
    assert(line.valid && offset + decoded.memory_width <= line.data.size());
    u8* const bytes = line.data.data() + offset;
    if (decoded.kind == CachedKind::Store) {
        const u64 value =
            decoded.floating_memory
                ? decoded.memory_width == 4 ? fpu.read_word(decoded.rt) : fpu.read_doubleword(decoded.rt)
                : gpr[decoded.rt];
        switch (decoded.memory_width) {
        case 1:
            *bytes = static_cast<u8>(value);
            break;
        case 2:
            write_be16(bytes, static_cast<u16>(value));
            break;
        case 4:
            write_be32(bytes, static_cast<u32>(value));
            break;
        case 8:
            write_be64(bytes, value);
            break;
        default:
            assert(false);
            return;
        }
        line.dirty = true;
        return;
    }

    u64 value = 0;
    switch (decoded.memory_width) {
    case 1:
        value = decoded.signed_load ? sign_extend8(*bytes) : *bytes;
        break;
    case 2: {
        const u16 raw = read_be16(bytes);
        value = decoded.signed_load ? sign_extend16(raw) : raw;
        break;
    }
    case 4: {
        const u32 raw = read_be32(bytes);
        value = decoded.signed_load ? sign_extend32(raw) : raw;
        break;
    }
    case 8:
        value = read_be64(bytes);
        break;
    default:
        assert(false);
        return;
    }
    if (decoded.floating_memory) {
        if (decoded.memory_width == 4)
            fpu.write_word(decoded.rt, static_cast<u32>(value));
        else
            fpu.write_doubleword(decoded.rt, value);
    } else if (decoded.rt != 0) {
        gpr[decoded.rt] = value;
    }
}

} // namespace cupid
