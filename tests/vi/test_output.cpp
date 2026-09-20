#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <utility>
#include <vector>

namespace {
using namespace cupid;

struct OutputFixture {
    System system;
    u64 clocks{};
    u64 cycles{};
    u32 start;

    explicit OutputFixture(VideoStandard standard = VideoStandard::Ntsc)
        : system(standard), start(standard == VideoStandard::Pal ? 44U : 34U) {
        configure();
    }
    void write(unsigned index, u32 value) {
        system.bus.write(0x04400000U + index * 4, 4, value);
    }
    void configure() {
        const u32 left = system.video_standard() == VideoStandard::Pal ? 128U : 108U;
        write(0, 0x303);
        write(1, 0x8000);
        write(2, 16);
        write(3, start);
        write(6, system.video_standard() == VideoStandard::Pal ? 625U : 525U);
        write(7, 99);
        write(8, (100U << 16) | 100U);
        write(9, (left << 16) | (left + 17));
        write(10, (start << 16) | (start + 2));
        write(12, 1024);
        write(13, 1024);
    }
    void pixel(u32 color, u32 origin = 0x8000) {
        for (unsigned byte = 0; byte < 4; ++byte)
            system.bus.rdram[origin + 32 + byte] = static_cast<u8>(color >> ((3 - byte) * 8));
    }
    u32 current() {
        return static_cast<u32>(system.bus.read(0x04400010, 4));
    }
    u64 rcp_time(u64 video_clocks) const {
        return (video_clocks * 62500000 + system.video_frequency() - 1) / system.video_frequency();
    }
    void advance(u64 video_clocks) {
        clocks += video_clocks;
        const u64 next = rcp_time(clocks);
        system.bus.tick(next - cycles);
        cycles = next;
    }
};
} // namespace

TEST(vi_output_occurs_at_vertical_start_with_current_pixels_and_interrupt) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        OutputFixture fixture(standard);
        auto& bus = fixture.system.bus;
        unsigned calls = 0;
        bus.set_video_output([&](VideoField field) {
            ++calls;
            CHECK_EQ(fixture.current(), fixture.start);
            CHECK_EQ(bus.read(0x04100010, 4), fixture.rcp_time(fixture.start / 2 * 100));
            CHECK_EQ(bus.read(0x04300008, 4) & 8U, 8U);
            CHECK_EQ(field.height, standard == VideoStandard::Pal ? 288U : 240U);
            CHECK_EQ(field.pixels[8], 0x123456ffU);
        });
        fixture.advance(fixture.start / 2 * 100 - 1);
        CHECK_EQ(calls, 0U);
        fixture.pixel(0x12345600);
        fixture.advance(1);
        CHECK_EQ(calls, 1U);
        fixture.advance(1000);
        CHECK_EQ(calls, 1U);
    }
}

TEST(vi_output_full_field_trace_preserves_regional_fraction_parity_and_leaps) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (bool interlaced : {false, true}) {
            const auto run = [&](bool single) {
                OutputFixture fixture(standard);
                auto& bus = fixture.system.bus;
                const u32 total =
                    (standard == VideoStandard::Pal ? 625U : 525U) - static_cast<u32>(interlaced);
                fixture.write(0, 0x303U | (interlaced ? 64U : 0U));
                fixture.write(6, total);
                fixture.write(7, (0b10101U << 16) | 99U);
                fixture.write(8, (40U << 16) | 20U);
                std::vector<std::array<u64, 4>> actual;
                bus.set_video_output([&](VideoField field) {
                    actual.push_back({bus.read(0x04100010, 4), fixture.current(), field.field,
                                      static_cast<u64>(field.interlaced)});
                });
                std::vector<std::array<u64, 4>> expected;
                u64 clocks = 0;
                for (unsigned field = 0; field < 10; ++field) {
                    const u32 parity = interlaced ? field & 1U : 0;
                    const u32 leap = ((0b10101U >> (field % 5)) & 1U) != 0 ? 40U : 20U;
                    const u32 lines = (total + 2 - parity) / 2;
                    const u64 start_clocks = clocks + (fixture.start / 2 - 1) * 100 + leap;
                    expected.push_back({fixture.rcp_time(start_clocks), fixture.start + parity, parity,
                                        static_cast<u64>(interlaced)});
                    clocks += (lines - 1) * 100 + leap;
                }
                const u64 cycles = fixture.rcp_time(clocks);
                if (single)
                    for (u64 cycle = 0; cycle < cycles; ++cycle)
                        bus.tick(1);
                else
                    bus.tick(cycles);
                CHECK_EQ(actual, expected);
                return actual;
            };
            CHECK_EQ(run(false), run(true));
        }
    }
}

TEST(vi_output_uses_vertical_start_line_in_both_halfline_phases) {
    for (u32 low_bit : {0U, 1U}) {
        OutputFixture fixture;
        fixture.write(6, 524);
        fixture.write(10, ((fixture.start + low_bit) << 16) | (fixture.start + 2));
        std::vector<u32> lines;
        fixture.system.bus.set_video_output([&](VideoField) { lines.push_back(fixture.current()); });
        fixture.advance((263 + 262) * 100);
        CHECK_EQ(lines, (std::vector<u32>{34, 35}));
    }
}

TEST(vi_output_start_writes_are_compared_at_subsequent_line_boundaries) {
    OutputFixture fixture;
    std::vector<u32> lines;
    fixture.system.bus.set_video_output([&](VideoField) { lines.push_back(fixture.current()); });
    fixture.advance(1000);
    fixture.write(10, (20U << 16) | 22U);
    CHECK(lines.empty());
    fixture.advance(50);
    CHECK(lines.empty());
    fixture.write(10, (22U << 16) | 24U);
    fixture.advance(50);
    CHECK_EQ(lines, (std::vector<u32>{22}));
    fixture.write(10, (24U << 16) | 26U);
    fixture.advance(100);
    CHECK_EQ(lines, (std::vector<u32>{22, 24}));
    fixture.write(10, (22U << 16) | 24U);
    fixture.advance(100);
    CHECK_EQ(lines.size(), 2U);
}

TEST(vi_output_start_zero_occurs_at_counter_wrap_and_unreachable_start_is_silent) {
    for (u32 total : {5U, 1023U}) {
        OutputFixture fixture;
        fixture.write(6, total);
        fixture.write(10, 2);
        unsigned calls = 0;
        fixture.system.bus.set_video_output([&](VideoField) {
            ++calls;
            CHECK_EQ(fixture.current(), 0U);
        });
        CHECK_EQ(calls, 0U);
        const u32 lines = (total + 1) / 2;
        fixture.advance(lines * 100 - 1);
        CHECK_EQ(calls, 0U);
        fixture.advance(1);
        CHECK_EQ(calls, 1U);
        fixture.write(6, 5);
        fixture.write(10, (6U << 16) | 8U);
        fixture.advance(600);
        CHECK_EQ(calls, 1U);
    }
}

TEST(vi_output_blanking_suppresses_events_and_unblank_resumes_odd_field) {
    OutputFixture fixture;
    fixture.write(6, 524);
    fixture.advance(263 * 100);
    CHECK_EQ(fixture.current(), 1U);
    unsigned calls = 0;
    fixture.system.bus.set_video_output([&](VideoField field) {
        ++calls;
        CHECK_EQ(field.field, 1U);
    });
    fixture.write(0, 0);
    fixture.advance(100000);
    CHECK_EQ(calls, 0U);
    fixture.write(0, 0x303);
    fixture.advance(fixture.start / 2 * 100 - 1);
    CHECK_EQ(calls, 0U);
    fixture.advance(1);
    CHECK_EQ(calls, 1U);
}

TEST(vi_output_owns_pixels_and_samples_origin_at_each_delivery) {
    OutputFixture fixture;
    fixture.pixel(0x12345600);
    fixture.pixel(0xabcdef00, 0x9000);
    std::vector<VideoField> fields;
    fixture.system.bus.set_video_output([&](VideoField field) { fields.push_back(std::move(field)); });
    fixture.advance(fixture.start / 2 * 100);
    fixture.write(1, 0x9000);
    fixture.advance(263 * 100);
    CHECK_EQ(fields.size(), 2U);
    CHECK_EQ(fields[0].pixels[8], 0x123456ffU);
    CHECK_EQ(fields[1].pixels[8], 0xabcdefffU);
    fixture.pixel(0xffffffffU, 0x9000);
    CHECK_EQ(fields[1].pixels[8], 0xabcdefffU);
}

TEST(vi_output_subscription_does_not_replay_and_survives_reset) {
    OutputFixture fixture;
    fixture.advance(fixture.start / 2 * 100);
    unsigned calls = 0;
    fixture.system.bus.set_video_output([&](VideoField) { ++calls; });
    fixture.advance(100);
    CHECK_EQ(calls, 0U);
    fixture.system.reset();
    fixture.clocks = 0;
    fixture.cycles = 0;
    fixture.configure();
    fixture.advance(fixture.start / 2 * 100);
    CHECK_EQ(calls, 1U);
    fixture.system.bus.set_video_output({});
    fixture.advance(263 * 100);
    CHECK_EQ(calls, 1U);
}

TEST(vi_output_callback_can_replace_itself_and_change_registers) {
    OutputFixture fixture;
    auto& bus = fixture.system.bus;
    unsigned calls = 0;
    bus.set_video_output([&, marker = std::vector<u32>{0x12345678}](VideoField) {
        ++calls;
        bus.set_video_output([&](VideoField) { calls += 10; });
        CHECK_EQ(marker[0], 0x12345678U);
        fixture.write(10, ((fixture.start + 2) << 16) | (fixture.start + 4));
        fixture.write(7, 19);
    });
    fixture.advance(fixture.start / 2 * 100);
    CHECK_EQ(calls, 1U);
    fixture.advance(99);
    CHECK_EQ(calls, 1U);
    fixture.advance(1);
    CHECK_EQ(calls, 11U);
    CHECK_EQ(fixture.current(), fixture.start + 2);
    fixture.advance(20);
    CHECK_EQ(fixture.current(), fixture.start + 4);
}

TEST(vi_output_preserves_mutable_callback_state_between_fields) {
    OutputFixture fixture;
    std::vector<unsigned> sequence;
    fixture.system.bus.set_video_output([&, count = 0U](VideoField) mutable { sequence.push_back(++count); });
    fixture.advance(3 * 263 * 100);
    CHECK_EQ(sequence, (std::vector<unsigned>{1, 2, 3}));
}

TEST(vi_output_callback_can_blank_after_a_zero_duration_leap) {
    OutputFixture fixture;
    fixture.write(8, 0);
    fixture.write(10, (2U << 16) | 4U);
    unsigned calls = 0;
    fixture.system.bus.set_video_output([&](VideoField) {
        ++calls;
        CHECK_EQ(fixture.current(), 2U);
        fixture.write(0, 0);
    });
    fixture.advance(100);
    CHECK_EQ(calls, 1U);
    CHECK_EQ(fixture.current(), 0U);
    fixture.advance(100000);
    CHECK_EQ(calls, 1U);
}

TEST(vi_output_reserved_type_delivers_black_without_stopping_timing) {
    OutputFixture fixture;
    fixture.pixel(0x12345600);
    fixture.write(0, 1);
    unsigned calls = 0;
    fixture.system.bus.set_video_output([&](VideoField field) {
        ++calls;
        CHECK_EQ(fixture.current(), fixture.start);
        for (u32 pixel : field.pixels)
            CHECK_EQ(pixel, 255U);
    });
    fixture.advance(fixture.start / 2 * 100);
    CHECK_EQ(calls, 1U);
}

TEST(vi_output_preserves_cpu_tick_size_independence_with_dma_audio_and_refresh) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (bool refresh : {false, true}) {
            const auto run = [&](bool single) {
                OutputFixture fixture(standard);
                auto& system = fixture.system;
                auto& bus = system.bus;
                test::initialize_memory(system);
                fixture.write(6, 63);
                bus.write(0x04700010, 4, refresh ? 0x20301U : 0U);
                bus.write(0x04000000, 4, 0x12345678);
                bus.write(0x04000004, 4, 0x23456789);
                system.rsp.write_register(0, 0);
                system.rsp.write_register(4, 0x8020);
                system.rsp.write_register(0x0c, 7);
                bus.pif[0x7c0] = 0xfe;
                bus.write(0x04800000, 4, 0x8020);
                bus.write(0x04800004, 4, 0x1fc007c0);
                const std::array<u64, 6> commands{
                    (0x3fULL << 56) | (3ULL << 51) | (15ULL << 32) | 0x8000,
                    (0x2fULL << 56) | (3ULL << 52),
                    (0x37ULL << 56) | 0xabcdefe0,
                    (0x2dULL << 56) | (64ULL << 12) | 4,
                    (0x36ULL << 56) | (32ULL << 44) | (32ULL << 12),
                    0x29ULL << 56,
                };
                for (u32 index = 0; index < commands.size(); ++index)
                    bus.write(0x1000U + index * 8, 8, commands[index]);
                unsigned samples = 0;
                bus.audio_output = [&](s16, s16) {
                    if (++samples == 100) {
                        bus.rdp.write_register(0, 0x1000);
                        bus.rdp.write_register(4, 0x1030);
                        CHECK_EQ(bus.rdp.current(), 0x1030U);
                    }
                };
                bus.write(0x04500010, 4, 26);
                bus.write(0x04500008, 4, 1);
                bus.write(0x04500000, 4, 0x9000);
                bus.write(0x04500004, 4, 512);
                std::vector<std::array<u64, 6>> trace;
                bus.set_video_output([&](VideoField field) {
                    trace.push_back({bus.read(0x04100010, 4), fixture.current(), field.pixels[8], samples,
                                     bus.memory.bank_status(), bus.read(0x04800018, 4)});
                });
                if (single)
                    for (u64 cycle = 0; cycle < 40000; ++cycle)
                        system.advance(1);
                else
                    system.advance(40000);
                CHECK_EQ(trace.size(), 6U);
                CHECK_EQ(trace.front()[0], fixture.rcp_time(fixture.start / 2 * 100));
                CHECK_EQ(trace.front()[2], 0x123456ffU);
                CHECK(trace.front()[3] != 0);
                CHECK_EQ(trace.front()[5] & 1U, 1U);
                CHECK_EQ(trace[1][2], 0xabcdefffU);
                CHECK_EQ(trace.back()[2], 0xfe0000ffU);
                CHECK_EQ(trace.back()[3], 128U);
                CHECK_EQ(trace.back()[5] & 1U, 0U);
                return trace;
            };
            CHECK_EQ(run(false), run(true));
        }
    }
}

TEST(vi_output_observation_preserves_final_device_and_memory_state) {
    const auto run = [](bool observe) {
        OutputFixture fixture;
        auto& bus = fixture.system.bus;
        test::initialize_memory(fixture.system);
        fixture.write(0, 0x307);
        fixture.pixel(0x7f7f7f00);
        bus.write(0x04700010, 4, 0x20301);
        bus.write(0x04500010, 4, 26);
        bus.write(0x04500008, 4, 1);
        bus.write(0x04500000, 4, 0x8000);
        bus.write(0x04500004, 4, 512);
        unsigned calls = 0;
        if (observe)
            bus.set_video_output([&](VideoField) { ++calls; });
        fixture.system.advance(120000);
        CHECK_EQ(calls, observe ? 3U : 0U);
        const std::array<u64, 6> state{fixture.current(),        bus.read(0x04300008, 4),
                                       bus.read(0x04100010, 4),  bus.read(0x04500004, 4),
                                       bus.memory.bank_status(), bus.rdram_refresh_wait()};
        return std::pair{state, bus.scan_video().pixels};
    };
    CHECK_EQ(run(false), run(true));
}
