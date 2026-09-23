#include "cupid/rdram.hpp"

#include <algorithm>
#include <stdexcept>

namespace cupid {

void Rdram::read_burst(u32 address, std::span<u8> bytes) const {
    if (bytes.size() != 16 && bytes.size() != 32)
        throw std::invalid_argument("Invalid RDRAM burst size");
    // An aligned cache line stays in one row and chip. Its words reach memory
    // at the same settled clock, so one access records their complete row effect.
    if (identity_mapping_ && (address & (bytes.size() - 1U)) == 0 &&
        static_cast<u64>(address) + bytes.size() <= bytes_.size()) {
        track_access(address, false);
        std::copy_n(bytes_.data() + address, bytes.size(), bytes.data());
        return;
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        write_be32(bytes.data() + offset, static_cast<u32>(read(address + static_cast<u32>(offset), 4)));
    }
}

void Rdram::write_burst(u32 address, std::span<const u8> bytes) {
    if (bytes.size() != 16 && bytes.size() != 32)
        throw std::invalid_argument("Invalid RDRAM burst size");
    if (identity_mapping_ && (address & (bytes.size() - 1U)) == 0 &&
        static_cast<u64>(address) + bytes.size() <= bytes_.size()) {
        track_access(address, true);
        std::copy_n(bytes.data(), bytes.size(), bytes_.data() + address);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 2)
            hidden_[(address + offset) >> 1U] = static_cast<u8>((bytes[offset + 1U] & 1U) * 3U);
        return;
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4)
        write(address + static_cast<u32>(offset), 4, read_be32(bytes.data() + offset));
}

} // namespace cupid
