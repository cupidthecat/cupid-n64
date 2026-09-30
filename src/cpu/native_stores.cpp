#include "cupid/cpu.hpp"

#include <cassert>

namespace cupid {
namespace {

u64 read_bytes(const u8* bytes, unsigned width) {
    switch (width) {
    case 1:
        return *bytes;
    case 2:
        return read_be16(bytes);
    case 4:
        return read_be32(bytes);
    case 8:
        return read_be64(bytes);
    default:
        assert(false);
        return 0;
    }
}

void write_bytes(u8* bytes, unsigned width, u64 value) {
    switch (width) {
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
    }
}

} // namespace

void CpuNativeState::commit_stores() {
    assert(store_count <= stores.size());
    // Every address guard has passed. Ordered writes preserve overlapping stores.
    for (unsigned index = 0; index < store_count; ++index) {
        auto& store = stores[index];
        auto& line = *static_cast<CacheLine<16>*>(store.line);
        assert(line.valid && store.offset + store.width <= line.data.size());
        u8* const bytes = line.data.data() + store.offset;
        store.previous = read_bytes(bytes, store.width);
        store.previous_dirty = line.dirty;
        write_bytes(bytes, store.width, store.value);
        line.dirty = true;
    }
}

void CpuNativeState::rollback_stores() {
    // An RSP interrupt can retire only a prefix. Restore in reverse before replay.
    while (store_count != 0U) {
        const auto& store = stores[--store_count];
        auto& line = *static_cast<CacheLine<16>*>(store.line);
        write_bytes(line.data.data() + store.offset, store.width, store.previous);
        line.dirty = store.previous_dirty;
    }
}

} // namespace cupid
