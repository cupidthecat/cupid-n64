#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;

void clock_program(System& system) {
    constexpr std::array<u32, 8> program = {0x40016000, 0,          0x00031940, 0,
                                            0x40026000, 0xac010000, 0xac020004, 0x0000000d};
    for (u32 index = 0; index < program.size(); ++index) {
        system.bus.write(0x04001000U + index * 4, 4, program[index]);
    }
    system.rsp.write_register(0x10, 1);
}
} // namespace

TEST(rcp_rsp_reads_dp_clock_at_each_instruction_boundary) {
    System system;
    clock_program(system);
    system.advance(12);
    const u64 before = system.bus.read(0x04000000, 4);
    const u64 after = system.bus.read(0x04000004, 4);
    CHECK_EQ(after - before, 4U);
    CHECK_EQ(system.rsp.pc, 28U);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 0U);
    // The second clock store used one interlock slot. BREAK is the ninth issue cycle.
    system.advance(2);
    CHECK_EQ(system.bus.read(0x04100010, 4), 9U);
    CHECK((system.rsp.read_register(0x10) & 3U) == 3U);
}

TEST(rcp_clock_reads_are_independent_of_cpu_batch_size) {
    System single;
    System split;
    clock_program(single);
    clock_program(split);
    single.advance(15);
    for (unsigned cycle = 0; cycle < 15; ++cycle)
        split.advance(1);
    CHECK_EQ(single.bus.read(0x04000000, 4), split.bus.read(0x04000000, 4));
    CHECK_EQ(single.bus.read(0x04000004, 4), split.bus.read(0x04000004, 4));
    CHECK_EQ(single.bus.read(0x04100010, 4), split.bus.read(0x04100010, 4));
}

TEST(rcp_audio_reads_memory_after_an_earlier_sp_dma_completion) {
    for (bool split : {false, true}) {
        System system;
        test::initialize_memory(system);
        std::vector<u32> samples;
        system.bus.audio_output = [&](s16 left, s16 right) {
            samples.push_back((static_cast<u32>(static_cast<u16>(left)) << 16) | static_cast<u16>(right));
        };
        system.bus.write(0x04000000, 4, 0x12345678);
        system.bus.write(0x04000004, 4, 0x23456789);
        system.rsp.write_register(0, 0);
        system.rsp.write_register(4, 0x2000);
        system.rsp.write_register(0x0c, 7);
        system.bus.write(0x04500010, 4, 7);
        system.bus.write(0x04500008, 4, 1);
        system.bus.write(0x04500000, 4, 0x2000);
        system.bus.write(0x04500004, 4, 8);
        if (split) {
            for (unsigned cycle = 0; cycle < 36; ++cycle)
                system.advance(1);
        } else {
            system.advance(36);
        }
        CHECK_EQ(samples.size(), 2U);
        CHECK_EQ(samples[0], 0x12345678U);
        CHECK_EQ(samples[1], 0x23456789U);
    }
}

TEST(rcp_audio_reads_memory_before_a_later_si_dma_completion) {
    for (bool direct_bus : {false, true}) {
        System system;
        test::initialize_memory(system);
        std::vector<u32> samples;
        system.bus.audio_output = [&](s16 left, s16 right) {
            samples.push_back((static_cast<u32>(static_cast<u16>(left)) << 16) | static_cast<u16>(right));
        };
        system.bus.write(0x2000, 4, 0x12345678);
        system.bus.write(0x2004, 4, 0x23456789);
        system.bus.pif[0x7c0] = 0xfe;
        system.bus.write(0x04800000, 4, 0x2000);
        system.bus.write(0x04800004, 4, 0x1fc007c0);
        system.bus.write(0x04500010, 4, 1103);
        system.bus.write(0x04500008, 4, 1);
        system.bus.write(0x04500000, 4, 0x2000);
        system.bus.write(0x04500004, 4, 8);
        if (direct_bus)
            system.bus.tick(20000);
        else
            system.advance(30000);
        CHECK_EQ(samples.size(), 2U);
        CHECK_EQ(samples[0], 0x12345678U);
        CHECK_EQ(samples[1], 0x23456789U);
        CHECK_EQ(system.bus.read(0x2000, 4), 0xfe000000U);
    }
}

TEST(rcp_audio_callbacks_observe_their_sample_clock) {
    System system;
    test::initialize_memory(system);
    std::vector<u64> clocks;
    system.bus.audio_output = [&](s16, s16) { clocks.push_back(system.bus.read(0x04100010, 4)); };
    system.bus.write(0x04500010, 4, 7);
    system.bus.write(0x04500008, 4, 1);
    system.bus.write(0x04500000, 4, 0x2000);
    system.bus.write(0x04500004, 4, 8);
    system.advance(36);
    CHECK_EQ(clocks.size(), 2U);
    CHECK_EQ(clocks[0], 11U);
    CHECK_EQ(clocks[1], 21U);
    CHECK_EQ(system.bus.read(0x04100010, 4), 24U);
}

TEST(rcp_sp_dma_finishes_before_a_later_si_read_of_ram) {
    for (bool split : {false, true}) {
        System system;
        test::initialize_memory(system);
        system.bus.write(0x04000000, 4, 0xfe123456);
        system.bus.write(0x04000004, 4, 0x23456789);
        system.rsp.write_register(0, 0);
        system.rsp.write_register(4, 0x2000);
        system.rsp.write_register(0x0c, 7);
        system.bus.write(0x04800000, 4, 0x2000);
        system.bus.write(0x04800010, 4, 0x1fc007c0);
        if (split) {
            for (unsigned cycle = 0; cycle < 7500; ++cycle)
                system.advance(1);
        } else {
            system.advance(7500);
        }
        CHECK_EQ(read_be32(system.bus.pif.data() + 0x7c0), 0xfe123456U);
        CHECK_EQ(read_be32(system.bus.pif.data() + 0x7c4), 0x23456789U);
    }
}

TEST(rcp_rsp_dma_does_not_overtake_earlier_rsp_instructions) {
    for (bool split : {false, true}) {
        System system;
        test::initialize_memory(system);
        system.bus.write(0x04000000, 4, 0x11112222);
        system.bus.write(0x2000, 4, 0x33334444);
        system.bus.write(0x04001000, 4, 0x8c010000); // LW at, 0(zero)
        system.bus.write(0x04001004, 4, 0xac010010); // SW at, 0x10(zero), outside the DMA row.
        system.bus.write(0x04001008, 4, 0x0000000d);
        system.rsp.write_register(0, 0);
        system.rsp.write_register(4, 0x2000);
        system.rsp.write_register(8, 15); // Two RCP cycles, after the first load.
        system.rsp.write_register(0x10, 1);
        if (split) {
            for (unsigned cycle = 0; cycle < 3; ++cycle)
                system.rsp.tick(1);
        } else {
            system.rsp.tick(3);
        }
        CHECK_EQ(system.bus.read(0x04000000, 4), 0x33334444U);
        CHECK_EQ(system.bus.read(0x04000010, 4), 0U);
        CHECK_EQ(system.rsp.pc, 4U);
        CHECK_EQ(system.rsp.read_register(0x18), 0U);
        // DMA completes during the two load-use stalls; the consumer still stores the old load.
        system.rsp.tick(1);
        CHECK_EQ(system.bus.read(0x04000010, 4), 0x11112222U);
        CHECK_EQ(system.rsp.pc, 8U);
        CHECK_EQ(system.rsp.read_register(0x10) & 3U, 0U);
        system.rsp.tick(1);
        CHECK_EQ(system.rsp.read_register(0x10) & 3U, 3U);
    }
}
