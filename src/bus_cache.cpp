#include "cupid/system.hpp"

#include <algorithm>
#include <stdexcept>

namespace cupid {

bool Bus::read_cache(u32 physical, std::span<u8> bytes) {
    if (bytes.size() != 16 && bytes.size() != 32)
        throw std::invalid_argument("Invalid cache line size");
    if (physical >= 0x04000000U || (mi_mode_ & 0x100U) != 0) {
        system_.cpu.frozen = true;
        return false;
    }
    if (physical >= 0x03f00000U) {
        std::fill(bytes.begin(), bytes.end(), u8{0});
        write_be32(bytes.data(), memory.read_register(physical));
        return true;
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        write_be32(bytes.data() + offset,
                   static_cast<u32>(memory.read(physical + static_cast<u32>(offset), 4)));
    }
    return true;
}

bool Bus::write_cache(u32 physical, std::span<const u8> bytes) {
    if (bytes.size() != 16 && bytes.size() != 32)
        throw std::invalid_argument("Invalid cache line size");
    if (physical >= 0x04000000U || (mi_mode_ & 0x100U) != 0) {
        system_.cpu.frozen = true;
        return false;
    }
    if (physical >= 0x03f00000U) {
        memory.write_register(physical, read_be32(bytes.data()));
        return true;
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        memory.write(physical + static_cast<u32>(offset), 4, read_be32(bytes.data() + offset));
    }
    return true;
}

} // namespace cupid
