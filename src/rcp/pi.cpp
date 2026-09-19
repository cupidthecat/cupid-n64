#include "cupid/bus.hpp"

namespace cupid {

unsigned Bus::pi_domain(u32 address) {
    const u32 region = address >> 24U;
    return region == 5 || (region >= 8 && region <= 15) ? 9U : 5U;
}

u32 Bus::pi_page_mask(u32 address) const {
    return (1U << (pi_[pi_domain(address) + 2] + 2)) - 1;
}

u64 Bus::pi_dma_cycles(u32 length) const {
    const unsigned domain = pi_domain(pi_[1]);
    const u64 bytes = static_cast<u64>(length | 1U) + 1;
    const u64 page_size = 1ULL << (pi_[domain + 2] + 2);
    const u64 offset = pi_[1] & (page_size - 1);
    const u64 pages = (offset + bytes - 1) / page_size + 1;
    u64 buffers = 0;
    u64 partial_bytes = 0;
    if (pages == 1) {
        if (bytes == 128)
            buffers = 1;
        else
            partial_bytes = bytes;
    } else {
        if (offset == 0)
            ++buffers;
        else
            partial_bytes += page_size - offset;
        const u64 tail = (offset + bytes) % page_size;
        if (tail == 0)
            ++buffers;
        else
            partial_bytes += tail;
        buffers += (pages - 2) * page_size / 128;
    }
    const u64 page_cycles = (15 + pi_[domain]) * pages;
    const u64 halfword_cycles = (pi_[domain + 1] + pi_[domain + 3] + 2) * (bytes / 2);
    return page_cycles + halfword_cycles + buffers * 28 + partial_bytes;
}

} // namespace cupid
