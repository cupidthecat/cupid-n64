#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;
constexpr u32 ViControl = 0x04400000;
constexpr u32 ViOrigin = 0x04400004;
constexpr u32 ViWidth = 0x04400008;
constexpr u32 ViVSync = 0x04400018;
constexpr u32 ViHSync = 0x0440001c;
constexpr u32 ViVVideo = 0x04400028;
constexpr u32 ViYScale = 0x04400034;
constexpr u32 Refresh = 0x04700010;
constexpr u32 RiSelect = 0x0470000c;
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

TEST(cpu_rdram_vi_fetch_keeps_its_line_boundary_in_bulk_ticks) {
    System bulk;
    System single;
    for (System* system : {&bulk, &single}) {
        prepare(*system, 0xffffffffa0300000ULL);
        enable_framebuffer(*system, 0x310000);
    }
    const u64 line = (3094ULL * 62500000 + bulk.video_frequency() - 1) / bulk.video_frequency();
    const u64 elapsed = line + fetch_interval(bulk) + 8;
    bulk.bus.tick(elapsed);
    for (u64 cycle = 0; cycle < elapsed; ++cycle)
        single.bus.tick(1);
    CHECK_EQ(bulk.bus.memory.bank_access_clock(0x310000), single.bus.memory.bank_access_clock(0x310000));
    CHECK_EQ(bulk.bus.read(BankStatus, 4), single.bus.read(BankStatus, 4));
}

TEST(cpu_rdram_vi_fetch_switches_banks_and_stops_when_disabled) {
    System system;
    prepare(system, 0xffffffffa0300000ULL);
    enable_framebuffer(system, 0x310000);
    const u64 interval = fetch_interval(system);
    system.bus.tick(interval);
    const u64 first_bank_clock = system.bus.memory.bank_access_clock(0x310000);
    CHECK_EQ(first_bank_clock, interval);

    system.bus.write(ViOrigin, 4, 0x410000);
    system.bus.tick(interval);
    const u64 second_bank_clock = system.bus.memory.bank_access_clock(0x410000);
    CHECK_EQ(second_bank_clock, interval * 2);
    CHECK_EQ(system.bus.memory.bank_access_clock(0x310000), first_bank_clock);

    system.bus.write(ViControl, 4, 0);
    system.bus.tick(interval * 3);
    CHECK_EQ(system.bus.memory.bank_access_clock(0x410000), second_bank_clock);
    system.bus.write(ViControl, 4, 2);
    system.bus.tick(interval - 1);
    CHECK_EQ(system.bus.memory.bank_access_clock(0x410000), second_bank_clock);
    system.bus.tick(1);
    CHECK_EQ(system.bus.memory.bank_access_clock(0x410000), interval * 6);
}

TEST(cpu_rdram_vi_register_writes_do_not_postpone_the_next_fetch) {
    for (unsigned variant = 0; variant < 5; ++variant) {
        System system;
        prepare(system, 0xffffffffa0300000ULL);
        enable_framebuffer(system, 0x310000);
        const u64 interval = fetch_interval(system);
        system.bus.tick(interval - 1);
        switch (variant) {
        case 0:
            system.bus.write(ViOrigin, 4, 0x410000);
            break;
        case 1:
            system.bus.write(ViVSync, 4, 0x20d);
            break;
        case 2:
            system.bus.write(ViHSync, 4, 3094);
            break;
        case 3:
            system.bus.write(ViControl, 4, 0x102);
            break;
        default:
            system.bus.write(RiSelect, 4, 0x14);
            break;
        }
        system.bus.tick(1);
        const u32 address = variant == 0 ? 0x410000 : 0x310000;
        CHECK_EQ(system.bus.memory.bank_access_clock(address), interval);
    }
}

TEST(cpu_rdram_uncached_read_completes_across_a_vi_fetch) {
    System system;
    const u32 physical = 0x300000;
    prepare(system, 0xffffffffa0000000ULL | physical);
    enable_framebuffer(system, 0x310000);
    system.bus.write(physical, 4, 0x12345678);
    system.bus.tick(fetch_interval(system) - 2);
    CHECK(!system.bus.rdram_row_miss(physical));
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[2], 0x12345678U);
    CHECK_EQ(system.cpu.cycles, 32U);
    CHECK_EQ(system.bus.memory.bank_access_clock(physical), system.bus.memory.clock());
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

TEST(cpu_rdram_vi_clock_batches_preserve_rows_and_individual_access_times) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (u32 format : {2U, 3U}) {
            System bulk(standard), single(standard);
            for (System* system : {&bulk, &single}) {
                prepare(*system, 0xffffffffa0300000ULL);
                enable_framebuffer(*system, 0x0ffc00);
                system->bus.write(ViWidth, 4, 4095);
                system->bus.write(ViControl, 4, format);
                system->bus.write(0x0ffc00, 4, 1);
                system->bus.write(0x100000, 4, 1);
            }
            for (u64 elapsed : {7U, 257U, 1700U, 1850U}) {
                bulk.bus.tick(elapsed);
                for (u64 cycle = 0; cycle < elapsed; ++cycle)
                    single.bus.tick(1);
                CHECK_EQ(bulk.bus.memory.bank_status(), single.bus.memory.bank_status());
                for (u32 bank = 0; bank < 8; ++bank) {
                    CHECK_EQ(bulk.bus.memory.bank_access_clock(bank << 20),
                             single.bus.memory.bank_access_clock(bank << 20));
                    for (u32 row = 0; row < 512; ++row)
                        CHECK_EQ(bulk.bus.memory.row_open((bank << 20) | (row << 11)),
                                 single.bus.memory.row_open((bank << 20) | (row << 11)));
                }
            }
            CHECK(bulk.bus.memory.bank_access_clock(0) != 0);
            CHECK(bulk.bus.memory.bank_access_clock(0x100000) != 0);
        }
    }
}

TEST(cpu_rdram_vi_clock_batch_discards_fetch_on_refresh_edge) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        System system(standard);
        prepare(system, 0xffffffffa0300000ULL);
        enable_framebuffer(system, 0x310000);
        system.bus.write(ViWidth, 4, 4095);
        system.bus.write(ViControl, 4, 3);
        system.bus.write(Refresh, 4, 0x007e3634U);
        const u64 line = (3094ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
        system.bus.tick(line);
        // This width fetches every RCP cycle. The horizontal edge cancels the
        // old line's coincident fetch, then refresh closes the last accessed row.
        CHECK_EQ(system.bus.memory.bank_access_clock(0x310000), line - 1);
        CHECK_EQ(system.bus.memory.bank_status() & 0xffU, 0U);
        system.bus.tick(1);
        CHECK_EQ(system.bus.memory.bank_access_clock(0x310000), line + 1);
    }
}

TEST(cpu_rdram_vi_clock_batches_precede_audio_dma_and_callbacks) {
    using Observation = std::array<u64, 4>;
    System bulk, single;
    std::array<std::vector<Observation>, 2> observations;
    for (unsigned index = 0; index < 2; ++index) {
        auto& system = index == 0 ? bulk : single;
        prepare(system, 0xffffffffa0300000ULL);
        enable_framebuffer(system, 0x310000);
        system.bus.write(ViWidth, 4, 4095);
        system.bus.write(ViControl, 4, 3);
        system.bus.write(0x04500010, 4, 29);
        system.bus.write(0x04500000, 4, 0x300000);
        system.bus.write(0x04500004, 4, 64);
        system.bus.write(0x04500008, 4, 1);
        system.bus.set_audio_sample_output([&, index](const AudioSample& sample) {
            const auto& memory = (index == 0 ? bulk : single).bus.memory;
            observations[index].push_back({sample.rcp_cycle, memory.bank_access_clock(0x300000),
                                           memory.row_open(0x300000), sample.from_dma});
        });
    }
    bulk.bus.tick(900);
    for (unsigned cycle = 0; cycle < 900; ++cycle)
        single.bus.tick(1);
    CHECK(observations[0] == observations[1]);
    CHECK(observations[0].size() > 16U);
    for (unsigned index = 0; index < 16; ++index) {
        CHECK_EQ(observations[0][index][0], observations[0][index][1]);
        CHECK_EQ(observations[0][index][2], 1U);
        CHECK_EQ(observations[0][index][3], 1U);
    }
    CHECK_EQ(observations[0][16][2], 0U);
    CHECK_EQ(observations[0][16][3], 0U);
}
