#include "cupid/rdram.hpp"

namespace cupid {

Rdram::Halfword Rdram::read_halfword(u32 address) const {
    address &= ~1U;
    if (!identity_mapping_ || static_cast<u64>(address) + 2U > bytes_.size())
        return {static_cast<u16>(read(address, 2)), hidden_pair(address)};

    // The initialized linear mapping contains complete halfwords. A framebuffer
    // cell carries its two hidden bits through the same row access as its value.
    track_access(address, false);
    return {read_be16(bytes_.data() + address), static_cast<u8>(hidden_[address >> 1U] & 3U)};
}

void Rdram::write_halfword(u32 address, Halfword value) {
    address &= ~1U;
    if (!identity_mapping_ || static_cast<u64>(address) + 2U > bytes_.size()) {
        write(address, 2, value.value);
        set_hidden_pair(address, value.hidden);
        return;
    }

    track_access(address, true);
    write_be16(bytes_.data() + address, value.value);
    hidden_[address >> 1U] = value.hidden & 3U;
}

} // namespace cupid
