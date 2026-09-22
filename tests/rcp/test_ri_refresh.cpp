#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;
constexpr u32 Refresh = 0x04700010;
constexpr u32 BankStatus = 0x0470001c;
constexpr u32 HSync = 0x0440001c;

void prepare(System& system, bool enable) {
    test::initialize_memory(system);
    system.bus.write(0x04400018, 4, 525);
    system.bus.write(HSync, 4, 3093);
    system.bus.write(0x04400020, 4, (3094U << 16) | 3094U);
    system.bus.write(0x04400000, 4, 2);
    system.bus.write(Refresh, 4, enable ? 0x007e3634U : 0x007c3634U);
    for (u32 bank = 0; bank < 8; ++bank)
        system.bus.write(bank * 0x100000, 4, 0x12345678U + bank);
}

u64 first_line(const System& system) {
    return (3094ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
}
} // namespace

TEST(ri_refresh_closes_open_rows_after_horizontal_sync) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        System system(standard);
        prepare(system, true);
        system.bus.tick(first_line(system) - 1);
        CHECK_EQ(system.bus.read(BankStatus, 4) & 0xffU, 0xffU);
        system.bus.tick(101);
        CHECK_EQ(system.bus.read(BankStatus, 4) & 0xffU, 0U);
        for (u32 bank = 0; bank < 8; ++bank)
            CHECK_EQ(system.bus.read(bank * 0x100000, 4), 0x12345678U + bank);
    }
}

TEST(ri_disabled_refresh_keeps_open_rows_across_horizontal_sync) {
    System system;
    prepare(system, false);
    system.bus.tick(first_line(system) * 4);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0xffffU);
}

TEST(ri_refresh_uses_the_selected_clean_or_dirty_delay) {
    for (bool dirty : {false, true}) {
        System system;
        prepare(system, true);
        system.bus.write(Refresh, 4, 0x007e2d17);
        if (!dirty) {
            for (u32 bank = 0; bank < 8; ++bank)
                CHECK_EQ(system.bus.read(bank * 0x100000 + 0x800, 4), 0U);
        }
        system.bus.tick(first_line(system));
        const u64 delay = dirty ? 45U : 23U;
        CHECK_EQ(system.bus.rdram_refresh_wait(), delay);
        system.bus.tick(delay - 1);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 1U);
        system.bus.tick(1);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    }
}

TEST(ri_refresh_continues_with_video_output_disabled) {
    System system;
    prepare(system, true);
    system.bus.write(0x04400000, 4, 0);
    system.bus.tick(first_line(system));
    CHECK_EQ(system.bus.read(BankStatus, 4) & 0xffU, 0U);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 54U);
    CHECK_EQ(system.bus.read(0x04400010, 4), 0U);
    CHECK_EQ(system.bus.read(0x04300008, 4) & 8U, 0U);
}

TEST(ri_disabling_refresh_does_not_cancel_an_active_recovery_delay) {
    System system;
    prepare(system, true);
    system.bus.tick(first_line(system));
    system.bus.write(Refresh, 4, 0);
    system.bus.tick(31);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 23U);
    system.bus.tick(23);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    system.bus.write(0, 4, 0xabcdef01);
    system.bus.tick(first_line(system));
    CHECK_EQ(system.bus.read(BankStatus, 4) & 1U, 1U);
}

TEST(ri_refresh_timing_is_independent_of_tick_size) {
    System bulk;
    System single;
    prepare(bulk, true);
    prepare(single, true);
    const u64 elapsed = first_line(bulk) * 3 + 17;
    bulk.bus.tick(elapsed);
    for (u64 cycle = 0; cycle < elapsed; ++cycle)
        single.bus.tick(1);
    CHECK_EQ(bulk.bus.rdram_refresh_wait(), single.bus.rdram_refresh_wait());
    CHECK_EQ(bulk.bus.read(BankStatus, 4), single.bus.read(BankStatus, 4));
    CHECK_EQ(bulk.bus.read(0x04400010, 4), single.bus.read(0x04400010, 4));
}

TEST(ri_refresh_reset_cancels_recovery_and_restores_the_horizontal_period) {
    System system;
    prepare(system, true);
    system.bus.tick(first_line(system));
    CHECK_EQ(system.bus.rdram_refresh_wait(), 54U);
    system.reset();
    CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    CHECK_EQ(system.bus.read(Refresh, 4), 0U);
    CHECK_EQ(system.bus.read(HSync, 4), 2047U);
}

TEST(ri_refresh_stalls_blocking_cpu_memory_requests_but_not_cache_hits_or_device_reads) {
    for (unsigned access = 0; access < 4; ++access) {
        System system;
        prepare(system, true);
        system.cpu.write_cop0(12, 0x34000000);
        system.cpu.set_pc(0xffffffff80001000ULL);
        system.cpu.gpr[1] = access == 0   ? 0xffffffffa0002000ULL
                            : access == 3 ? 0xffffffffa4300004ULL
                                          : 0xffffffff80002000ULL;
        system.bus.write(0x1000, 4, 0x8c220000);
        system.bus.write(0x2000, 4, 0x12345678);
        u64 ignored = 0;
        CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
        if (access == 2)
            CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, ignored));
        system.bus.tick(first_line(system));
        CHECK_EQ(system.bus.rdram_refresh_wait(), 54U);
        system.cpu.step();
        // The uncached read also opens its row again because the refresh closed every row.
        const u64 expected = access == 0 ? 116U : access == 1 ? 122U : access == 2 ? 1U : 5U;
        CHECK_EQ(system.cpu.cycles, expected);
        CHECK_EQ(system.cpu.gpr[2], access == 3 ? 0x02020102U : 0x12345678U);
        CHECK_EQ(system.cpu.cp0[9], expected / 2);
    }
}

TEST(ri_refresh_delays_an_instruction_cache_miss_once) {
    System system;
    prepare(system, true);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.bus.tick(first_line(system));
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 130U);
    CHECK_EQ(system.cpu.pc, 0xffffffff80001004ULL);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 131U);
}

TEST(ri_refresh_speculative_fetch_wait_follows_the_older_device_sample) {
    System system;
    prepare(system, true);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff8000101cULL);
    system.cpu.gpr[1] = 0xffffffffa4100010ULL;
    system.bus.write(0x101c, 4, 0x8c220000);
    cupid::u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
    const u64 line = first_line(system);
    system.bus.tick(line);
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[2], line + 3);
    CHECK_EQ(system.cpu.cycles, 129U);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 130U);
}

TEST(ri_refresh_delays_a_dirty_cache_writeback) {
    System system;
    prepare(system, true);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.gpr[1] = 0xffffffff80002000ULL;
    system.bus.write(0x1000, 4, (0x2fU << 26) | (1U << 21) | (0x19U << 16));
    CHECK(system.cpu.write_memory(system.cpu.gpr[1], 4, 0x12345678));
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
    system.bus.tick(first_line(system));
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 121U);
    CHECK_EQ(system.bus.read(0x2000, 4), 0x12345678U);
}

TEST(ri_refresh_delays_explicit_instruction_cache_transfers) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (u32 operation : {0x14U, 0x18U}) {
            for (u32 delay : {23U, 54U}) {
                System system(standard);
                prepare(system, true);
                system.bus.write(Refresh, 4, 0x20000U | (delay << 8) | delay);
                system.cpu.write_cop0(12, 0x34000000);
                system.cpu.set_pc(0xffffffff80001000ULL);
                system.cpu.gpr[1] = 0xffffffff80002000ULL;
                system.bus.write(0x1000, 4, (0x2fU << 26) | (1U << 21) | (operation << 16));
                for (u32 offset = 0; offset < 32; offset += 4)
                    system.bus.write(0x2000 + offset, 4, 0x12345678U + offset);
                u64 ignored = 0;
                CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
                if (operation == 0x18) {
                    CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, ignored, true));
                    for (u32 offset = 0; offset < 32; offset += 4)
                        system.bus.write(0x2000 + offset, 4, 0);
                }
                system.bus.tick(first_line(system));
                CHECK_EQ(system.bus.rdram_refresh_wait(), delay);
                system.cpu.step();
                const u64 expected = (operation == 0x14 ? 50U : 49U) + (delay * 3 - 1) / 2;
                CHECK_EQ(system.cpu.cycles, expected);
                CHECK_EQ(system.cpu.cp0[9], expected / 2);
                CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
                for (u32 offset = 0; offset < 32; offset += 4) {
                    if (operation == 0x14) {
                        CHECK(system.cpu.read_memory(system.cpu.gpr[1] + offset, 4, ignored, true));
                        CHECK_EQ(ignored, 0x12345678U + offset);
                    } else {
                        CHECK_EQ(system.bus.read(0x2000 + offset, 4), 0x12345678U + offset);
                    }
                }
            }
        }
    }
}

TEST(ri_refresh_does_not_delay_instruction_cache_operations_without_a_transfer) {
    for (u32 operation : {0x00U, 0x04U, 0x08U, 0x10U, 0x18U}) {
        System system;
        prepare(system, true);
        system.cpu.write_cop0(12, 0x34000000);
        system.cpu.set_pc(0xffffffff80001000ULL);
        system.cpu.gpr[1] = 0xffffffff80002000ULL;
        system.bus.write(0x1000, 4, (0x2fU << 26) | (1U << 21) | (operation << 16));
        u64 ignored = 0;
        CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
        system.bus.tick(first_line(system));
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, 1U);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 54U);
        CHECK_EQ(system.bus.read(BankStatus, 4) & 0xffU, 0U);
    }
}

TEST(ri_refresh_does_not_delay_instruction_cache_transfers_to_chip_registers) {
    for (u32 operation : {0x14U, 0x18U}) {
        System system;
        prepare(system, true);
        system.cpu.write_cop0(12, 0x34000000);
        system.cpu.set_pc(0xffffffff80001000ULL);
        system.cpu.gpr[1] = 0xffffffff83f00020ULL;
        system.bus.write(0x1000, 4, (0x2fU << 26) | (1U << 21) | (operation << 16));
        const u64 register_value = system.bus.read(0x03f00020, 4);
        u64 ignored = 0;
        CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
        if (operation == 0x18) {
            CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, ignored, true));
        }
        system.bus.tick(first_line(system));
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, operation == 0x14 ? 50U : 49U);
        CHECK_EQ(system.bus.rdram_refresh_wait(), operation == 0x14 ? 21U : 22U);
        if (operation == 0x14) {
            CHECK(system.cpu.read_memory(system.cpu.gpr[1], 4, ignored, true));
            CHECK_EQ(ignored, register_value);
        } else {
            CHECK_EQ(system.bus.read(0x03f00020, 4), register_value);
        }
    }
}
