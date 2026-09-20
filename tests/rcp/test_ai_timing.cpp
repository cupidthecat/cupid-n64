#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {
using namespace cupid;

struct AudioTiming {
    System system;
    u64 cycles{};
    std::vector<u64> samples;

    explicit AudioTiming(VideoStandard standard = VideoStandard::Ntsc, bool programmed = true,
                         bool queued = true)
        : system(standard) {
        test::initialize_memory(system);
        system.bus.audio_output = [&](s16, s16) { samples.push_back(system.bus.read(0x04100010, 4)); };
        if (programmed)
            system.bus.write(0x04500010, 4, 99);
        system.bus.write(0x04500008, 4, 1);
        system.bus.write(0x04500000, 4, 0x2000);
        if (queued)
            system.bus.write(0x04500004, 4, 32);
    }
    u64 time(u64 video_clocks) const {
        return (video_clocks * 62500000 + system.video_frequency() - 1) / system.video_frequency();
    }
    void until(u64 next) {
        CHECK(next >= cycles);
        system.bus.tick(next - cycles);
        cycles = next;
    }
    void video(u32 period, u32 total = 63) {
        system.bus.write(0x04400000, 4, 1);
        system.bus.write(0x04400018, 4, total);
        system.bus.write(0x0440001c, 4, period - 1);
        system.bus.write(0x04400020, 4, (period << 16) | period);
        system.bus.write(0x04400028, 4, (2U << 16) | 4U);
    }
};
} // namespace

TEST(ai_divider_shrink_preserves_the_pending_sample_deadline) {
    AudioTiming fixture;
    fixture.until(fixture.time(40));
    fixture.system.bus.write(0x04500010, 4, 19);
    fixture.until(fixture.time(100) - 1);
    CHECK(fixture.samples.empty());
    CHECK_EQ(fixture.system.bus.read(0x04500004, 4), 32U);
    fixture.until(fixture.time(100));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(100)}));
    fixture.until(fixture.time(120));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(100), fixture.time(120)}));
}

TEST(ai_divider_growth_preserves_the_pending_sample_deadline) {
    AudioTiming fixture;
    fixture.until(fixture.time(40));
    fixture.system.bus.write(0x04500010, 4, 159);
    fixture.until(fixture.time(100));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(100)}));
    fixture.until(fixture.time(260) - 1);
    CHECK_EQ(fixture.samples.size(), 1U);
    fixture.until(fixture.time(260));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(100), fixture.time(260)}));
}

TEST(ai_divider_latches_the_latest_pending_value_with_register_masking) {
    AudioTiming fixture;
    fixture.until(fixture.time(40));
    fixture.system.bus.write(0x04500010, 4, 19);
    fixture.until(fixture.time(60));
    fixture.system.bus.write(0x04500010, 4, 0xffffc09fU);
    fixture.until(fixture.time(260));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(100), fixture.time(260)}));
    CHECK_EQ(fixture.system.bus.read(0x04500010, 4), 24U);
}

TEST(ai_divider_writes_before_elapsed_time_configure_the_first_sample) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (u32 divider : {0U, 16383U}) {
            AudioTiming fixture(region);
            fixture.system.bus.write(0x04500010, 4, divider);
            fixture.system.bus.tick(0);
            fixture.until(fixture.time(divider + 1) - 1);
            CHECK(fixture.samples.empty());
            fixture.until(fixture.time(divider + 1));
            CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(divider + 1)}));
        }
    }
}

TEST(ai_divider_changes_keep_running_while_dma_is_disabled) {
    AudioTiming fixture;
    auto& bus = fixture.system.bus;
    bus.write(0x04500008, 4, 0);
    fixture.until(fixture.time(40));
    bus.write(0x04500010, 4, 159);
    fixture.until(fixture.time(200));
    CHECK(fixture.samples.empty());
    CHECK_EQ(bus.read(0x04500004, 4), 32U);
    bus.write(0x04500008, 4, 1);
    fixture.until(fixture.time(260) - 1);
    CHECK(fixture.samples.empty());
    fixture.until(fixture.time(260));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(260)}));
}

TEST(ai_divider_changes_keep_phase_while_the_fifo_is_empty) {
    AudioTiming fixture(VideoStandard::Ntsc, true, false);
    fixture.until(fixture.time(140));
    fixture.system.bus.write(0x04500010, 4, 159);
    fixture.until(fixture.time(300));
    fixture.system.bus.write(0x04500004, 4, 8);
    fixture.until(fixture.time(360) - 1);
    CHECK(fixture.samples.empty());
    fixture.until(fixture.time(360));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(360)}));
    fixture.until(fixture.time(520));
    CHECK_EQ(fixture.samples.size(), 2U);
}

TEST(ai_first_programmed_rate_preserves_the_default_clock_fraction) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        AudioTiming fixture(region, false);
        fixture.until(500);
        fixture.system.bus.write(0x04500010, 4, 19);
        const u64 denominator = static_cast<u64>(fixture.system.video_frequency()) * 44100;
        const u64 first = 62500000ULL * fixture.system.video_frequency();
        std::vector<u64> expected;
        for (u64 sample = 0; sample < 8; ++sample)
            expected.push_back((first + sample * 62500000ULL * 44100 * 20 + denominator - 1) / denominator);
        fixture.until(expected.front() - 1);
        CHECK(fixture.samples.empty());
        fixture.until(expected.back());
        CHECK_EQ(fixture.samples, expected);
    }
}

TEST(ai_reset_discards_active_and_pending_divider_state) {
    AudioTiming fixture;
    fixture.until(fixture.time(40));
    fixture.system.bus.write(0x04500010, 4, 19);
    fixture.system.reset();
    fixture.cycles = 0;
    fixture.system.bus.write(0x04500008, 4, 1);
    fixture.system.bus.write(0x04500004, 4, 8);
    fixture.until(1417);
    CHECK(fixture.samples.empty());
    fixture.until(1418);
    CHECK_EQ(fixture.samples, (std::vector<u64>{1418}));
    fixture.system.reset();
    fixture.cycles = 0;
    fixture.samples.clear();
    fixture.system.bus.write(0x04500010, 4, 19);
    fixture.system.bus.write(0x04500008, 4, 1);
    fixture.system.bus.write(0x04500004, 4, 8);
    fixture.until(fixture.time(20));
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(20)}));
}

TEST(ai_divider_changes_preserve_fifo_handoff_and_sample_values) {
    AudioTiming fixture(VideoStandard::Ntsc, true, false);
    auto& bus = fixture.system.bus;
    bus.write(0x2000, 4, 0x80007fff);
    bus.write(0x2004, 4, 0x1234fedc);
    bus.write(0x3000, 4, 0x43211234);
    bus.write(0x3004, 4, 0x8765abcd);
    bus.write(0x04500004, 4, 8);
    bus.write(0x04500000, 4, 0x3000);
    bus.write(0x04500004, 4, 8);
    bus.write(0x0450000c, 4, 0);
    std::vector<std::array<u64, 3>> trace;
    bus.audio_output = [&](s16 left, s16 right) {
        const u32 sample = (static_cast<u32>(static_cast<u16>(left)) << 16) | static_cast<u16>(right);
        trace.push_back({bus.read(0x04100010, 4), sample, bus.read(0x04300008, 4) & 4U});
    };
    fixture.until(fixture.time(140));
    bus.write(0x04500010, 4, 19);
    fixture.until(fixture.time(240));
    CHECK_EQ(trace, (std::vector<std::array<u64, 3>>{{fixture.time(100), 0x80007fff, 0},
                                                     {fixture.time(200), 0x1234fedc, 0},
                                                     {fixture.time(220), 0x43211234, 4},
                                                     {fixture.time(240), 0x8765abcd, 4}}));
    CHECK_EQ(bus.read(0x04500004, 4), 0U);
    CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0U);
}

TEST(ai_audio_callback_rate_writes_select_the_following_period) {
    AudioTiming fixture;
    auto& bus = fixture.system.bus;
    const std::array<u32, 4> periods{20, 160, 1, 100};
    bus.audio_output = [&](s16, s16) {
        fixture.samples.push_back(bus.read(0x04100010, 4));
        bus.write(0x04500010, 4, periods[(fixture.samples.size() - 1) % periods.size()] - 1);
    };
    fixture.until(fixture.time(281));
    CHECK_EQ(fixture.samples,
             (std::vector<u64>{fixture.time(100), fixture.time(120), fixture.time(280), fixture.time(281)}));
}

TEST(ai_video_callback_does_not_retime_the_first_elapsed_audio_period) {
    AudioTiming fixture;
    fixture.video(40);
    unsigned calls = 0;
    fixture.system.bus.set_video_output([&](VideoField) {
        ++calls;
        fixture.system.bus.write(0x04500010, 4, 19);
        fixture.system.bus.set_video_output({});
    });
    fixture.until(fixture.time(120));
    CHECK_EQ(calls, 1U);
    CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(100), fixture.time(120)}));
}

TEST(ai_video_callback_does_not_backdate_new_audio_into_elapsed_empty_periods) {
    for (u32 period : {20U, 100U}) {
        AudioTiming fixture(VideoStandard::Ntsc, true, false);
        fixture.system.bus.write(0x04500010, 4, period - 1);
        fixture.video(175);
        unsigned calls = 0;
        fixture.system.bus.set_video_output([&](VideoField) {
            ++calls;
            fixture.system.bus.write(0x04500010, 4, 19);
            fixture.system.bus.write(0x04500004, 4, 8);
            fixture.system.bus.set_video_output({});
        });
        fixture.until(fixture.time(240));
        CHECK_EQ(calls, 1U);
        const u64 first = period == 20 ? 180U : 200U;
        CHECK_EQ(fixture.samples, (std::vector<u64>{fixture.time(first), fixture.time(first + 20)}));
    }
}

TEST(ai_rate_changes_preserve_cpu_tick_size_independence_with_video_dma_and_refresh) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        const auto run = [&](bool single) {
            AudioTiming fixture(region, true, false);
            auto& system = fixture.system;
            auto& bus = system.bus;
            fixture.video(97, 5);
            bus.write(0x04700010, 4, 0x20301);
            for (u32 index = 0; index < 64; ++index)
                bus.write(0x04000000U + index * 4, 4, 0x12340000U + index);
            system.rsp.write_register(0, 0);
            system.rsp.write_register(4, 0x2000);
            system.rsp.write_register(0x0c, 255);
            bus.pif[0x7c0] = 0xfe;
            bus.write(0x04800000, 4, 0x2080);
            bus.write(0x04800004, 4, 0x1fc007c0);
            bus.write(0x04500004, 4, 256);
            unsigned fields = 0;
            bus.set_video_output([&](VideoField) { bus.write(0x04500010, 4, 199 + (fields++ % 7) * 40); });
            std::vector<std::array<u64, 4>> trace;
            bus.audio_output = [&](s16 left, s16 right) {
                const u32 sample = (static_cast<u32>(static_cast<u16>(left)) << 16) | static_cast<u16>(right);
                trace.push_back(
                    {bus.read(0x04100010, 4), sample, bus.read(0x04400010, 4), bus.memory.bank_status()});
            };
            if (single)
                for (unsigned cycle = 0; cycle < 60000; ++cycle)
                    system.advance(1);
            else
                system.advance(60000);
            CHECK_EQ(trace.size(), 64U);
            CHECK_EQ(trace.front()[0], fixture.time(100));
            CHECK_EQ(trace.front()[1], 0x12340000U);
            CHECK_EQ(trace.back()[1], 0x1234003fU);
            CHECK(std::any_of(trace.begin(), trace.end(), [](const auto& sample) { return sample[1] == 0; }));
            CHECK_EQ(bus.read(0x04500004, 4), 0U);
            return trace;
        };
        CHECK_EQ(run(false), run(true));
    }
}

TEST(ai_large_empty_advances_preserve_phase_without_integer_overflow) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        for (u32 divider : {0U, 16383U}) {
            AudioTiming fixture(region, true, false);
            fixture.system.bus.write(0x04500010, 4, divider);
            constexpr u64 elapsed = 20000000;
            fixture.until(elapsed);
            const u64 periods = elapsed * fixture.system.video_frequency() / (62500000ULL * (divider + 1));
            const u64 deadline = fixture.time((periods + 1) * (divider + 1));
            fixture.system.bus.write(0x04500004, 4, 8);
            fixture.until(deadline - 1);
            CHECK(fixture.samples.empty());
            fixture.until(deadline);
            CHECK_EQ(fixture.samples, (std::vector<u64>{deadline & 0x00ffffffU}));
        }
    }
}
