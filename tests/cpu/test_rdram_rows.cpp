#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;
constexpr u32 ViControl = 0x04400000;
constexpr u32 ViOrigin = 0x04400004;
constexpr u32 ViWidth = 0x04400008;
constexpr u32 ViHSync = 0x0440001c;
constexpr u32 ViVVideo = 0x04400028;
constexpr u32 ViYScale = 0x04400034;
constexpr u32 Refresh = 0x04700010;
constexpr u32 BankStatus = 0x0470001c;

// LW v0, 0(at) from a program line in bank 0; the operand address selects the bank under test.
void prepare(System& system, u64 address) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.gpr[1] = address;
    system.bus.write(0x1000, 4, 0x8c220000);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
}

void enable_framebuffer(System& system, u32 origin) {
    system.bus.write(ViHSync, 4, 3093);
    system.bus.write(ViVVideo, 4, (0x25U << 16) | 0x1ffU);
    system.bus.write(ViYScale, 4, 0x400);
    system.bus.write(ViWidth, 4, 320);
    system.bus.write(ViOrigin, 4, origin);
    system.bus.write(ViControl, 4, 2);
}

// One eight-byte word per interval: a 3094-clock line moved to RCP cycles, times 8, over 640 bytes.
u64 fetch_interval(const System& system) {
    const u64 line = (3094ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
    return line * 8 / 640;
}
} // namespace

TEST(cpu_rdram_uncached_read_waits_for_a_closed_row_in_its_bank) {
    for (u32 bank = 0; bank < 8; ++bank) {
        System system;
        const u32 physical = bank * 0x100000U + 0x2000;
        prepare(system, 0xffffffffa0000000ULL | physical);
        system.bus.write(physical, 4, 0x12345678);
        CHECK_EQ(system.bus.read(physical + 0x1000, 4), 0U);
        CHECK(system.bus.rdram_row_miss(physical));
        system.cpu.step();
        CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
        CHECK_EQ(system.cpu.cycles, 36U);
        CHECK_EQ(system.cpu.cp0[9], 18U);
        CHECK(!system.bus.rdram_row_miss(physical));

        system.cpu.set_pc(0xffffffff80001000ULL);
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, 68U);
    }
}

TEST(cpu_rdram_row_wait_ignores_other_banks_and_chip_registers) {
    System system;
    prepare(system, 0xffffffffa0002000ULL);
    system.bus.write(0x2000, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x103000, 4), 0U);
    CHECK(!system.bus.rdram_row_miss(0x2000));
    CHECK(!system.bus.rdram_row_miss(0x03f00000));
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 32U);
}

TEST(cpu_rdram_cached_refill_does_not_add_the_row_wait) {
    System system;
    prepare(system, 0xffffffff80002000ULL);
    system.bus.write(0x2000, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x3000, 4), 0U);
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
    CHECK_EQ(system.cpu.cycles, 42U);
}

TEST(cpu_rdram_standalone_reads_stay_untimed_across_rows) {
    System system;
    prepare(system, 0xffffffffa0002000ULL);
    system.bus.write(0x2000, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x3000, 4), 0U);
    u64 value = 0;
    CHECK(system.cpu.read_memory(0xffffffffa0002000ULL, 4, value));
    CHECK_EQ(value, 0x12345678U);
    CHECK_EQ(system.cpu.cycles, 0U);
}

TEST(cpu_rdram_refresh_closes_rows_so_the_next_uncached_read_reopens_one) {
    System system;
    prepare(system, 0xffffffffa0002000ULL);
    system.bus.write(ViHSync, 4, 3093);
    system.bus.write(0x2000, 4, 0x12345678);
    system.bus.write(Refresh, 4, 0x007e3634U);
    CHECK(!system.bus.rdram_row_miss(0x2000));
    const u64 line = (3094ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
    system.bus.tick(line + 60);
    CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    CHECK_EQ(system.bus.read(BankStatus, 4) & 1U, 0U);
    CHECK(system.bus.rdram_row_miss(0x2000));
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 36U);
}

TEST(cpu_rdram_vi_fetches_replace_the_open_row_in_the_framebuffer_bank) {
    for (bool same_bank : {true, false}) {
        System system;
        const u32 physical = 0x300000;
        prepare(system, 0xffffffffa0000000ULL | physical);
        enable_framebuffer(system, same_bank ? 0x310000 : 0x410000);
        system.bus.write(physical, 4, 0x12345678);
        system.bus.tick(fetch_interval(system) * 3);
        CHECK_EQ(system.bus.rdram_row_miss(physical), same_bank);
        system.cpu.step();
        CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
        CHECK_EQ(system.cpu.cycles, same_bank ? 36U : 32U);
    }
}

TEST(cpu_rdram_vi_fetch_needs_an_interval_and_a_selected_framebuffer_type) {
    for (unsigned variant = 0; variant < 3; ++variant) {
        System system;
        const u32 physical = 0x300000;
        prepare(system, 0xffffffffa0000000ULL | physical);
        enable_framebuffer(system, 0x310000);
        if (variant == 1)
            system.bus.write(ViControl, 4, 0);
        if (variant == 2)
            system.bus.write(ViWidth, 4, 0);
        system.bus.write(physical, 4, 0x12345678);
        system.bus.tick(variant == 0 ? fetch_interval(system) - 1 : fetch_interval(system) * 3);
        CHECK(!system.bus.rdram_row_miss(physical));
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, 32U);
    }
}

TEST(cpu_rdram_vi_fetch_of_the_same_row_keeps_it_open) {
    System system;
    const u32 physical = 0x310000;
    prepare(system, 0xffffffffa0000000ULL | physical);
    enable_framebuffer(system, 0x310000);
    system.bus.write(physical, 4, 0x12345678);
    system.bus.tick(fetch_interval(system) * 3);
    CHECK(!system.bus.rdram_row_miss(physical));
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 32U);
}

TEST(cpu_rdram_vi_fetch_settlement_is_independent_of_tick_size) {
    System bulk;
    System single;
    for (System* system : {&bulk, &single}) {
        prepare(*system, 0xffffffffa0300000ULL);
        enable_framebuffer(*system, 0x310000);
        system->bus.write(0x300000, 4, 0x12345678);
    }
    const u64 elapsed = fetch_interval(bulk) * 5 + 7;
    bulk.bus.tick(elapsed);
    for (u64 cycle = 0; cycle < elapsed; ++cycle)
        single.bus.tick(1);
    CHECK_EQ(bulk.bus.rdram_row_miss(0x300000), single.bus.rdram_row_miss(0x300000));
    CHECK(bulk.bus.rdram_row_miss(0x300000));
    CHECK_EQ(bulk.bus.read(BankStatus, 4), single.bus.read(BankStatus, 4));
    bulk.cpu.step();
    single.cpu.step();
    CHECK_EQ(bulk.cpu.cycles, single.cpu.cycles);
}

TEST(cpu_rdram_reset_restarts_the_row_clock_with_every_row_closed) {
    System system;
    prepare(system, 0xffffffffa0300000ULL);
    enable_framebuffer(system, 0x310000);
    system.bus.write(0x300000, 4, 0x12345678);
    system.bus.tick(fetch_interval(system) * 5 + 7);
    CHECK(system.bus.memory.clock() != 0);

    system.reset();
    CHECK_EQ(system.bus.memory.clock(), 0U);
    prepare(system, 0xffffffffa0300000ULL);
    system.bus.write(0x300000, 4, 0x12345678);
    CHECK(!system.bus.rdram_row_miss(0x300000));
    system.bus.tick(fetch_interval(system) * 5 + 7);
    CHECK(!system.bus.rdram_row_miss(0x300000));
    CHECK_EQ(system.bus.memory.bank_access_clock(0x300000), 0U);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 32U);
}
