#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>
#include <vector>

namespace {
using namespace cupid;

using Stereo = std::array<s16, 2>;

void initialize(System& system, bool program_rate = true, u32 length = 8) {
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.memory.write(0x2000, 4, 0x80007fff);
    bus.memory.write(0x2004, 4, 0x4000e000);
    bus.memory.write(0x2008, 4, 0x2000c000);
    bus.memory.write(0x200c, 4, 0x00000000);
    if (program_rate)
        bus.write(0x04500010, 4, 4095);
    bus.write(0x04500000, 4, 0x2000);
    bus.write(0x04500008, 4, 1);
    bus.write(0x04500004, 4, length);
    bus.write(0x0450000c, 4, 0);
}

u64 deadline(const System& system, u64 video_clocks) {
    return (video_clocks * 62500000U + system.video_frequency() - 1U) / system.video_frequency();
}

void advance_to(System& system, u64 clock, bool single = false) {
    const u64 remaining = clock - system.bus.output_clock();
    if (single) {
        for (u64 cycle = 0; cycle < remaining; ++cycle)
            system.bus.tick(1);
    } else {
        system.bus.tick(remaining);
    }
}

Stereo stereo(const AudioSample& sample) {
    return {sample.left, sample.right};
}
} // namespace

TEST(ai_dac_empty_fifo_discharges_the_last_stereo_sample) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        auto system = std::make_unique<System>(region);
        initialize(*system);
        auto& bus = system->bus;
        std::vector<AudioSample> samples;
        std::vector<Stereo> dma;
        bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
        bus.audio_output = [&](s16 left, s16 right) { dma.push_back({left, right}); };
        advance_to(*system, deadline(*system, 5U * 4096U));
        CHECK_EQ(samples.size(), 5U);
        CHECK_EQ(dma, (std::vector<Stereo>{{-32768, 32767}, {16384, -8192}}));
        CHECK_EQ(stereo(samples[0]), dma[0]);
        CHECK_EQ(stereo(samples[1]), dma[1]);
        // A 3 ms discharge sampled after one, two and three 4096-clock intervals.
        const std::array<Stereo, 3> expected =
            region == VideoStandard::Ntsc
                ? std::array<Stereo, 3>{{{15930, -7965}, {15490, -7745}, {15061, -7530}}}
                : std::array<Stereo, 3>{{{15939, -7969}, {15507, -7753}, {15086, -7543}}};
        for (unsigned index = 0; index < samples.size(); ++index) {
            CHECK_EQ(samples[index].rcp_cycle, deadline(*system, (index + 1ULL) * 4096U));
            CHECK_EQ(samples[index].rate_numerator, system->video_frequency());
            CHECK_EQ(samples[index].rate_denominator, 4096U);
            CHECK_EQ(samples[index].from_dma, index < 2);
            if (index >= 2)
                CHECK_EQ(stereo(samples[index]), expected[index - 2U]);
        }
        CHECK_EQ(bus.read(0x04500004, 4), 0U);
        CHECK_EQ(bus.read(0x04300008, 4) & 4U, 0U);
        CHECK_EQ(bus.memory.bank_access_clock(0x2000), deadline(*system, 2U * 4096U));
    }
}

TEST(ai_dac_disabled_dma_discharges_without_consuming_the_queued_sample) {
    auto system = std::make_unique<System>();
    initialize(*system, true, 16);
    auto& bus = system->bus;
    std::vector<AudioSample> samples;
    unsigned dma = 0;
    bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
    bus.audio_output = [&](s16, s16) { ++dma; };
    advance_to(*system, deadline(*system, 2U * 4096U));
    bus.write(0x04500008, 4, 0);
    advance_to(*system, deadline(*system, 3U * 4096U));
    CHECK_EQ(stereo(samples.back()), (Stereo{15930, -7965}));
    CHECK(!samples.back().from_dma);
    CHECK_EQ(bus.read(0x04500004, 4), 8U);
    CHECK_EQ(bus.memory.bank_access_clock(0x2000), deadline(*system, 2U * 4096U));
    CHECK_EQ(dma, 2U);
    bus.write(0x04500008, 4, 1);
    advance_to(*system, deadline(*system, 4U * 4096U));
    CHECK_EQ(stereo(samples.back()), (Stereo{8192, -16384}));
    CHECK(samples.back().from_dma);
    CHECK_EQ(bus.read(0x04500004, 4), 4U);
    bus.write(0x04500008, 4, 0);
    advance_to(*system, deadline(*system, 5U * 4096U));
    CHECK_EQ(stereo(samples.back()), (Stereo{7965, -15930}));
    CHECK_EQ(dma, 3U);
}

TEST(ai_dac_rate_changes_decay_over_the_period_already_latched) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        auto system = std::make_unique<System>(region);
        initialize(*system);
        std::vector<AudioSample> samples;
        auto& bus = system->bus;
        bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
        advance_to(*system, deadline(*system, 2U * 4096U));
        bus.write(0x04500010, 4, 8191);
        advance_to(*system, deadline(*system, 3U * 4096U));
        CHECK_EQ(samples.size(), 3U);
        CHECK_EQ(samples.back().rate_denominator, 4096U);
        CHECK_EQ(samples.back().left, region == VideoStandard::Ntsc ? 15930 : 15939);
        advance_to(*system, deadline(*system, 3U * 4096U + 8192U));
        CHECK_EQ(samples.size(), 4U);
        CHECK_EQ(samples.back().rate_denominator, 8192U);
        CHECK_EQ(samples.back().left, region == VideoStandard::Ntsc ? 15061 : 15086);
    }
}

TEST(ai_dac_late_observers_see_elapsed_decay_after_bulk_or_single_cycle_advances) {
    for (bool single : {false, true}) {
        auto system = std::make_unique<System>();
        initialize(*system);
        advance_to(*system, deadline(*system, 101U * 4096U), single);
        std::vector<AudioSample> samples;
        system->bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
        advance_to(*system, deadline(*system, 102U * 4096U));
        CHECK_EQ(samples.size(), 1U);
        CHECK_EQ(stereo(samples[0]), (Stereo{991, -495}));
        CHECK(!samples[0].from_dma);
    }
}

TEST(ai_dac_zero_length_retirement_keeps_the_existing_discharge) {
    auto system = std::make_unique<System>();
    initialize(*system);
    std::vector<AudioSample> samples;
    auto& bus = system->bus;
    bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
    advance_to(*system, deadline(*system, 2U * 4096U));
    bus.write(0x04500008, 4, 0);
    bus.write(0x04500004, 4, 0);
    bus.write(0x0450000c, 4, 0);
    advance_to(*system, deadline(*system, 3U * 4096U));
    CHECK_EQ(stereo(samples.back()), (Stereo{15930, -7965}));
    CHECK(!samples.back().from_dma);
    CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0U);
    CHECK_EQ(bus.read(0x04300008, 4) & 4U, 0U);
}

TEST(ai_dac_default_clock_discharges_before_the_first_programmed_rate) {
    auto system = std::make_unique<System>();
    initialize(*system, false);
    std::vector<AudioSample> samples;
    auto& bus = system->bus;
    bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
    bus.tick((3ULL * 62500000U + 44099U) / 44100U);
    CHECK_EQ(samples.size(), 3U);
    CHECK_EQ(samples.back().rate_numerator, 44100U);
    CHECK_EQ(samples.back().rate_denominator, 1U);
    CHECK_EQ(stereo(samples.back()), (Stereo{16260, -8130}));
}

TEST(ai_dac_reset_and_long_unobserved_idle_do_not_retain_a_stale_output) {
    for (bool reset : {false, true}) {
        auto system = std::make_unique<System>();
        initialize(*system);
        advance_to(*system, deadline(*system, 2U * 4096U));
        if (reset)
            system->reset();
        else
            system->bus.tick(625000000U);
        std::vector<AudioSample> samples;
        system->bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
        system->bus.tick(reset ? 1500U : 6000U);
        CHECK_EQ(samples.size(), 1U);
        CHECK_EQ(stereo(samples[0]), (Stereo{0, 0}));
        CHECK(!samples[0].from_dma);
    }
}
