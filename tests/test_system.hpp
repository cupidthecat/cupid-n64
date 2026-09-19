#pragma once

#include "cupid/system.hpp"

namespace test {

inline void initialize_memory(cupid::System& system) {
    auto& memory = system.bus.memory;
    memory.reset();
    system.bus.write(0x04700008, 4, 0);
    system.bus.write(0x0470000c, 4, 0x14);
    memory.write_register(0x03f80008, 0x00080000, 16);
    const unsigned count = static_cast<unsigned>(system.bus.rdram.size() / 0x200000U);
    for (unsigned chip = 0; chip < count; ++chip) {
        const unsigned id = chip == 0 ? 8 : chip * 2;
        memory.write_register(0x03f00004, id << 26);
        memory.write_register(0x03f0000c + (id << 10), 0x02000000);
    }
    memory.write_register(0x03f02004, 0);
}

} // namespace test
