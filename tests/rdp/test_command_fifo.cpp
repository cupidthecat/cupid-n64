#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>

namespace {
using namespace cupid;

constexpr u64 full_sync = 0x2900000000000000ULL;
constexpr std::array<unsigned, 10> opcodes{0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x24, 0x25};
constexpr std::array<unsigned, 10> lengths{32, 48, 96, 112, 96, 112, 160, 176, 16, 16};

void submit(System& system, u32 address, unsigned bytes, bool dmem = false) {
    system.bus.rdp.write_register(12, dmem ? 2U : 1U);
    system.bus.rdp.write_register(0, address);
    system.bus.rdp.write_register(4, address + bytes);
}

void write_word(System& system, u32 address, u64 word, bool dmem) {
    if (!dmem) {
        system.bus.write(address, 8, word);
        return;
    }
    for (unsigned byte = 0; byte < 8; ++byte)
        system.rsp.memory[(address + byte) & 0xfffU] = static_cast<u8>(word >> ((7U - byte) * 8U));
}

bool dp_interrupt(System& system) {
    return (system.bus.read(0x04300008, 4) & 0x20U) != 0;
}
} // namespace

TEST(rdp_command_fifo_preserves_every_packet_split_across_new_dma_ranges) {
    for (unsigned packet = 0; packet < opcodes.size(); ++packet)
        for (unsigned split = 8; split < lengths[packet]; split += 8)
            for (bool switch_source : {false, true}) {
                System system;
                test::initialize_memory(system);
                system.bus.write(0x1000, 8, 0x2d00000000000000ULL); // Empty scissor.
                submit(system, 0x1000, 8);
                system.bus.write(0x2000, 8, static_cast<u64>(opcodes[packet]) << 56U);
                for (unsigned byte = 8; byte < split; byte += 8)
                    system.bus.write(0x2000 + byte, 8, full_sync);
                submit(system, 0x2000, split);
                CHECK(!dp_interrupt(system));

                const u32 tail = switch_source ? 0xff8U : 0x3000U;
                for (unsigned byte = 0; byte < lengths[packet] - split; byte += 8)
                    write_word(system, tail + byte, full_sync, switch_source);
                submit(system, tail, lengths[packet] - split, switch_source);
                CHECK_EQ(system.bus.rdp.current(), tail + lengths[packet] - split);
                CHECK(!dp_interrupt(system)); // Payload words must not become SyncFull commands.
                CHECK_EQ(system.bus.rdp.read_register(12) & 2U, 0U);

                system.bus.write(0x4000, 8, full_sync);
                submit(system, 0x4000, 8);
                CHECK(dp_interrupt(system));
            }
}

TEST(rdp_command_fifo_reset_discards_an_incomplete_packet) {
    System system;
    test::initialize_memory(system);
    system.bus.write(0x1000, 8, 0x0f00000000000000ULL);
    submit(system, 0x1000, 8);
    CHECK(!dp_interrupt(system));
    system.bus.rdp.reset();
    system.bus.write(0x2000, 8, full_sync);
    submit(system, 0x2000, 8);
    CHECK(dp_interrupt(system));
}
