#include "cupid/system.hpp"
#include "test.hpp"

namespace {
using namespace cupid;
constexpr u32 Control = 0x04400000;
constexpr u32 Interrupt = 0x0440000c;
constexpr u32 Current = 0x04400010;
constexpr u32 VerticalSync = 0x04400018;
constexpr u32 HorizontalSync = 0x0440001c;
constexpr u32 Leap = 0x04400020;
constexpr u32 MiInterrupt = 0x04300008;

void configure(Bus& bus) {
    bus.write(VerticalSync, 4, 525);
    bus.write(HorizontalSync, 4, 3093);
    bus.write(Leap, 4, (3094U << 16) | 3094U);
    bus.write(Control, 4, 2);
}

struct VideoClock {
    System& system;
    u64 video_clocks{};
    u64 rcp_clocks{};

    void advance(u64 clocks) {
        video_clocks += clocks;
        const u64 next = (video_clocks * 62500000 + system.video_frequency() - 1) / system.video_frequency();
        system.bus.tick(next - rcp_clocks);
        rcp_clocks = next;
    }
};
} // namespace

TEST(vi_scanline_uses_the_video_oscillator_and_terminal_count) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        System system(standard);
        configure(system.bus);
        system.bus.write(Interrupt, 4, 2);
        const u64 edge = (3094ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
        system.bus.tick(edge - 1);
        CHECK_EQ(system.bus.read(Current, 4), 0U);
        CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 0U);
        system.bus.tick(1);
        CHECK_EQ(system.bus.read(Current, 4), 2U);
        CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 8U);
    }
}

TEST(vi_clock_keeps_fractional_cycles_across_tick_sizes) {
    System bulk;
    System single;
    configure(bulk.bus);
    configure(single.bus);
    for (unsigned batch = 0; batch < 4; ++batch) {
        bulk.bus.tick(62501);
        for (unsigned cycle = 0; cycle < 62501; ++cycle)
            single.bus.tick(1);
        CHECK_EQ(bulk.bus.read(Current, 4), single.bus.read(Current, 4));
        CHECK_EQ(bulk.bus.read(MiInterrupt, 4), single.bus.read(MiInterrupt, 4));
    }
    const u64 lines = (4ULL * 62501 * bulk.video_frequency() / 62500000) / 3094;
    CHECK_EQ(bulk.bus.read(Current, 4), lines * 2);
}

TEST(vi_clock_stops_when_the_video_type_is_blank) {
    System system;
    configure(system.bus);
    system.bus.write(Interrupt, 4, 2);
    system.bus.write(Control, 4, 0);
    system.bus.tick(10000);
    CHECK_EQ(system.bus.read(Current, 4), 0U);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 0U);
    system.bus.write(Control, 4, 2);
    system.bus.tick(4000);
    CHECK_EQ(system.bus.read(Current, 4), 2U);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 8U);
    system.bus.write(Current, 4, 0xffffffffU);
    CHECK_EQ(system.bus.read(Current, 4), 2U);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 0U);
}

TEST(vi_clock_uses_the_five_field_leap_pattern) {
    System system;
    configure(system.bus);
    system.bus.write(VerticalSync, 4, 5);
    system.bus.write(HorizontalSync, 4, (0b10101U << 16) | 99U);
    system.bus.write(Leap, 4, (40U << 16) | 20U);
    VideoClock clock{system};
    for (unsigned field = 0; field < 10; ++field) {
        CHECK_EQ(system.bus.read(Current, 4), 0U);
        clock.advance(100);
        CHECK_EQ(system.bus.read(Current, 4), 2U);
        const u64 leap = ((0b10101U >> (field % 5)) & 1U) != 0 ? 40U : 20U;
        clock.advance(leap - 1);
        CHECK_EQ(system.bus.read(Current, 4), 2U);
        clock.advance(1);
        CHECK_EQ(system.bus.read(Current, 4), 4U);
        clock.advance(100);
    }
}

TEST(vi_clock_interlaced_fields_have_distinct_interrupt_comparisons) {
    for (u32 compare : {0U, 3U, 4U}) {
        System system;
        configure(system.bus);
        system.bus.write(VerticalSync, 4, 4);
        system.bus.write(HorizontalSync, 4, 99);
        system.bus.write(Leap, 4, (100U << 16) | 100U);
        system.bus.write(Interrupt, 4, compare);
        VideoClock clock{system};
        for (unsigned frame = 0; frame < 2; ++frame) {
            for (u32 current : {2U, 4U, 1U, 3U, 0U}) {
                system.bus.write(Current, 4, 0);
                clock.advance(100);
                CHECK_EQ(system.bus.read(Current, 4), current);
                const bool interrupt = compare == 0   ? current == 4 || current == 0
                                       : compare == 3 ? current == 2 || current == 3
                                                      : current == 4 || current == 3;
                CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, interrupt ? 8U : 0U);
            }
        }
    }
}

TEST(vi_clock_progressive_interrupt_ignores_the_comparator_low_bit) {
    System system;
    configure(system.bus);
    system.bus.write(VerticalSync, 4, 5);
    system.bus.write(HorizontalSync, 4, 99);
    system.bus.write(Leap, 4, (100U << 16) | 100U);
    system.bus.write(Interrupt, 4, 3);
    VideoClock clock{system};
    clock.advance(100);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 8U);
    system.bus.write(Current, 4, 0);
    clock.advance(200);
    CHECK_EQ(system.bus.read(Current, 4), 0U);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 0U);
    clock.advance(100);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 8U);
}

TEST(vi_clock_reset_clears_field_leap_and_fractional_state) {
    System system;
    configure(system.bus);
    system.bus.write(VerticalSync, 4, 4);
    system.bus.write(HorizontalSync, 4, 99);
    system.bus.write(Leap, 4, (100U << 16) | 100U);
    VideoClock clock{system};
    clock.advance(350);
    CHECK_EQ(system.bus.read(Current, 4), 1U);
    system.reset();
    CHECK_EQ(system.bus.read(Current, 4), 0U);
    CHECK_EQ(system.bus.read(Interrupt, 4), 256U);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 0U);
    configure(system.bus);
    system.bus.write(HorizontalSync, 4, (1U << 16) | 99U);
    system.bus.write(Leap, 4, (40U << 16) | 20U);
    clock.video_clocks = 0;
    clock.rcp_clocks = 0;
    clock.advance(100);
    CHECK_EQ(system.bus.read(Current, 4), 2U);
    clock.advance(39);
    CHECK_EQ(system.bus.read(Current, 4), 2U);
    clock.advance(1);
    CHECK_EQ(system.bus.read(Current, 4), 4U);
}

TEST(vi_clock_zero_timing_values_do_not_use_default_periods) {
    System system;
    configure(system.bus);
    system.bus.write(VerticalSync, 4, 5);
    system.bus.write(HorizontalSync, 4, 0);
    system.bus.write(Leap, 4, 0);
    system.bus.write(Interrupt, 4, 2);
    system.bus.tick(1);
    CHECK_EQ(system.bus.read(Current, 4), 0U);
    system.bus.tick(1);
    CHECK_EQ(system.bus.read(Current, 4), 4U);
    CHECK_EQ(system.bus.read(MiInterrupt, 4) & 8U, 8U);
}
