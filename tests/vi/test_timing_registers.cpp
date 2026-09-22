#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;

struct TimingFixture {
    System system;
    u64 clocks{};
    u64 cycles{};

    explicit TimingFixture(VideoStandard standard = VideoStandard::Ntsc) : system(standard) {
        write(0, 2);
        write(3, 1023);
        write(6, 5);
        write(7, 99);
        write(8, (100U << 16) | 100U);
    }
    void write(unsigned index, u32 value) {
        system.bus.write(0x04400000U + index * 4, 4, value);
    }
    u32 current() {
        return static_cast<u32>(system.bus.read(0x04400010, 4));
    }
    bool interrupt() {
        return (system.bus.read(0x04300008, 4) & 8U) != 0;
    }
    void advance(u64 video_clocks, bool single = false) {
        clocks += video_clocks;
        const u64 next = (clocks * 62500000 + system.video_frequency() - 1) / system.video_frequency();
        if (single)
            for (u64 count = cycles; count < next; ++count)
                system.bus.tick(1);
        else
            system.bus.tick(next - cycles);
        cycles = next;
    }
};
} // namespace

TEST(vi_horizontal_period_writes_do_not_retime_an_active_line) {
    for (u32 period : {20U, 160U}) {
        TimingFixture fixture;
        fixture.write(6, 15);
        fixture.advance(40);
        fixture.write(7, period - 1);
        CHECK_EQ(fixture.system.bus.read(0x0440001c, 4), period - 1);
        fixture.advance(59);
        CHECK_EQ(fixture.current(), 0U);
        fixture.advance(1);
        CHECK_EQ(fixture.current(), 2U);
        fixture.advance(100);
        CHECK_EQ(fixture.current(), 4U);
        fixture.advance(period - 1);
        CHECK_EQ(fixture.current(), 4U);
        fixture.advance(1);
        CHECK_EQ(fixture.current(), 6U);
    }
}

TEST(vi_leap_value_and_pattern_writes_apply_to_the_next_leap_line) {
    TimingFixture fixture;
    fixture.advance(140);
    CHECK_EQ(fixture.current(), 2U);
    fixture.write(7, (2U << 16) | 99U);
    fixture.write(8, (40U << 16) | 20U);
    fixture.advance(59);
    CHECK_EQ(fixture.current(), 2U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 4U);
    fixture.advance(200);
    CHECK_EQ(fixture.current(), 2U);
    fixture.advance(39);
    CHECK_EQ(fixture.current(), 2U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 4U);
}

TEST(vi_zero_period_write_waits_for_the_old_line_to_complete) {
    TimingFixture fixture;
    fixture.advance(40);
    fixture.write(7, 0);
    fixture.write(8, 0);
    fixture.advance(59);
    CHECK_EQ(fixture.current(), 0U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 4U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 0U);
}

TEST(vi_blanking_preserves_odd_field_and_pending_interrupt) {
    TimingFixture fixture;
    fixture.write(6, 4);
    fixture.write(3, 3);
    fixture.advance(400);
    CHECK_EQ(fixture.current(), 3U);
    CHECK(fixture.interrupt());
    fixture.write(0, 0);
    fixture.advance(100);
    CHECK_EQ(fixture.current(), 1U);
    CHECK(fixture.interrupt());
    fixture.write(4, 0);
    fixture.advance(1000);
    CHECK_EQ(fixture.current(), 1U);
    CHECK(!fixture.interrupt());
    fixture.write(0, 2);
    fixture.advance(100);
    CHECK_EQ(fixture.current(), 3U);
    CHECK(fixture.interrupt());
}

TEST(vi_blanking_takes_effect_at_the_latched_line_boundary) {
    TimingFixture fixture;
    fixture.write(6, 15);
    fixture.advance(240);
    CHECK_EQ(fixture.current(), 4U);
    fixture.write(0, 0);
    fixture.advance(59);
    CHECK_EQ(fixture.current(), 4U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 0U);
}

TEST(vi_blank_pulse_within_one_line_does_not_reset_vertical_count) {
    TimingFixture fixture;
    fixture.write(6, 15);
    fixture.advance(240);
    fixture.write(0, 0);
    fixture.advance(10);
    fixture.write(0, 2);
    fixture.advance(50);
    CHECK_EQ(fixture.current(), 6U);
}

TEST(vi_disabling_video_does_not_replace_an_active_leap_deadline) {
    TimingFixture fixture;
    fixture.write(8, (40U << 16) | 40U);
    fixture.advance(110);
    fixture.write(0, 0);
    fixture.advance(29);
    CHECK_EQ(fixture.current(), 2U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 0U);
    fixture.write(0, 2);
    fixture.advance(99);
    CHECK_EQ(fixture.current(), 0U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 2U);
}

TEST(vi_nine_bit_counter_wrap_does_not_advance_the_leap_pattern) {
    for (u32 total : {1022U, 1023U}) {
        TimingFixture fixture;
        fixture.write(6, total);
        fixture.write(7, (2U << 16) | 99U);
        fixture.write(8, (40U << 16) | 20U);
        fixture.advance(100 + 20 + 510 * 100);
        CHECK_EQ(fixture.current(), 0U);
        fixture.advance(100);
        CHECK_EQ(fixture.current(), 2U);
        fixture.advance(20);
        CHECK_EQ(fixture.current(), 4U);
    }
}

TEST(vi_vertical_total_changes_are_compared_at_the_next_line_boundary) {
    TimingFixture fixture;
    fixture.write(6, 15);
    fixture.advance(240);
    fixture.write(6, 4);
    CHECK_EQ(fixture.current(), 4U);
    fixture.advance(59);
    CHECK_EQ(fixture.current(), 4U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 1U);
}

TEST(vi_interrupt_target_changes_do_not_raise_retroactive_interrupts) {
    TimingFixture fixture;
    fixture.advance(100);
    fixture.write(3, 2);
    CHECK(!fixture.interrupt());
    fixture.advance(199);
    CHECK(!fixture.interrupt());
    fixture.advance(101);
    CHECK(fixture.interrupt());
    fixture.write(4, 0xffffffffU);
    CHECK(!fixture.interrupt());
    CHECK_EQ(fixture.current(), 2U);
}

TEST(vi_reset_discards_the_active_line_period) {
    TimingFixture fixture;
    fixture.advance(40);
    fixture.write(7, 19);
    fixture.system.reset();
    fixture.clocks = 0;
    fixture.cycles = 0;
    fixture.write(0, 2);
    fixture.write(6, 5);
    fixture.write(7, 19);
    fixture.write(8, (20U << 16) | 20U);
    fixture.advance(19);
    CHECK_EQ(fixture.current(), 0U);
    fixture.advance(1);
    CHECK_EQ(fixture.current(), 2U);
}

TEST(vi_latched_horizontal_period_keeps_refresh_on_the_same_boundary) {
    TimingFixture fixture;
    test::initialize_memory(fixture.system);
    fixture.system.bus.write(0x04700010, 4, 0x20000);
    fixture.system.bus.write(0, 4, 0x12345678);
    fixture.advance(40);
    fixture.write(7, 19);
    fixture.advance(59);
    CHECK_EQ(fixture.system.bus.memory.bank_status() & 1U, 1U);
    fixture.advance(1);
    CHECK_EQ(fixture.system.bus.memory.bank_status() & 1U, 0U);
}

TEST(vi_full_field_traces_preserve_region_fraction_and_leap_phase) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (bool interlaced : {false, true}) {
            TimingFixture bulk(standard);
            TimingFixture single(standard);
            const u32 total = (standard == VideoStandard::Pal ? 625U : 525U) - static_cast<u32>(interlaced);
            for (auto* fixture : {&bulk, &single}) {
                fixture->write(6, total);
                fixture->write(7, (0b10101U << 16) | 99U);
                fixture->write(8, (40U << 16) | 20U);
                fixture->write(3, 3);
            }
            for (unsigned field = 0; field < 10; ++field) {
                const u32 parity = interlaced ? field & 1U : 0;
                const u32 lines = (total + 2 - parity) / 2;
                CHECK_EQ(bulk.current(), parity);
                for (u32 line = 0; line < lines; ++line) {
                    const u32 period = line == 1 ? (((0b10101U >> (field % 5)) & 1U) != 0 ? 40U : 20U) : 100U;
                    bulk.write(4, 0);
                    single.write(4, 0);
                    bulk.advance(period);
                    single.advance(period, true);
                    const u32 expected =
                        line + 1 == lines ? parity ^ static_cast<u32>(interlaced) : (line + 1) * 2 + parity;
                    CHECK_EQ(bulk.current(), expected);
                    CHECK_EQ(single.current(), expected);
                    CHECK_EQ(bulk.interrupt(), line == 0);
                    CHECK_EQ(single.interrupt(), line == 0);
                }
            }
        }
    }
}

TEST(vi_register_changes_keep_tick_size_independence_with_refresh_and_dma) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        const auto run = [&](bool single) {
            TimingFixture fixture(standard);
            test::initialize_memory(fixture.system);
            auto& bus = fixture.system.bus;
            fixture.write(3, 3);
            bus.write(0x04700010, 4, 0x20000);
            for (u32 index = 0; index < 32; ++index)
                bus.write(0x04000000U + index * 4, 4, 0x12345678U + index);
            fixture.system.rsp.write_register(0, 0);
            fixture.system.rsp.write_register(4, 0x2000);
            fixture.system.rsp.write_register(0x0c, (15U << 12) | 7U);
            std::vector<std::array<u64, 5>> trace;
            bus.audio_output = [&](s16 left, s16 right) {
                const u32 sample = (static_cast<u32>(static_cast<u16>(left)) << 16) | static_cast<u16>(right);
                trace.push_back({fixture.current(), static_cast<u64>(fixture.interrupt()),
                                 bus.read(0x04100010, 4), sample, bus.memory.bank_status()});
                fixture.write(4, 0);
                switch (trace.size() % 6) {
                case 0:
                    fixture.write(7, 19);
                    break;
                case 1:
                    fixture.write(8, (31U << 16) | 17U);
                    break;
                case 2:
                    fixture.write(0, 0);
                    break;
                case 3:
                    fixture.write(0, 2);
                    break;
                case 4:
                    fixture.write(6, 4);
                    break;
                case 5:
                    fixture.write(7, 99);
                    break;
                }
            };
            bus.write(0x04500010, 4, 26);
            bus.write(0x04500008, 4, 1);
            bus.write(0x04500000, 4, 0x2000);
            bus.write(0x04500004, 4, 256);
            if (single)
                for (unsigned cycle = 0; cycle < 4000; ++cycle)
                    fixture.system.advance(1);
            else
                fixture.system.advance(4000);
            CHECK_EQ(trace.size(), 64U);
            CHECK_EQ(trace.front()[3], 0x12345678U);
            CHECK_EQ(bus.read(0x04500004, 4), 0U);
            return trace;
        };
        CHECK_EQ(run(false), run(true));
    }
}
