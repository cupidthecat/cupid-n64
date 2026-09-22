#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <memory>
#include <vector>

using namespace cupid;

namespace {

void start_samples(System& system, u32 dac_rate) {
    test::initialize_memory(system);
    system.bus.rdram[0] = 0x80;
    system.bus.rdram[1] = 0x01;
    system.bus.rdram[2] = 0x7f;
    system.bus.rdram[3] = 0xfe;
    system.bus.write(0x04500010, 4, dac_rate);
    system.bus.write(0x04500000, 4, 0);
    system.bus.write(0x04500008, 4, 1);
    system.bus.write(0x04500004, 4, 8);
}

std::vector<AudioSample> capture(bool single_ticks) {
    auto system = std::make_unique<System>();
    start_samples(*system, 1023);
    std::vector<AudioSample> samples;
    system->bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
    if (single_ticks) {
        for (unsigned tick = 0; tick < 3000; ++tick)
            system->bus.tick(1);
    } else {
        system->bus.tick(3000);
    }
    return samples;
}

} // namespace

TEST(audio_output_timestamps_and_rates_do_not_depend_on_tick_size) {
    const auto bulk = capture(false);
    const auto stepped = capture(true);
    CHECK_EQ(bulk.size(), 2U);
    CHECK_EQ(stepped.size(), bulk.size());
    for (std::size_t index = 0; index < bulk.size(); ++index) {
        CHECK_EQ(bulk[index].rcp_cycle, stepped[index].rcp_cycle);
        CHECK_EQ(bulk[index].left, stepped[index].left);
        CHECK_EQ(bulk[index].right, stepped[index].right);
        CHECK_EQ(bulk[index].rate_numerator, 48681818U);
        CHECK_EQ(bulk[index].rate_denominator, 1024U);
    }
    CHECK_EQ(bulk[0].left, -32767);
    CHECK_EQ(bulk[0].right, 32766);
    CHECK_EQ(bulk[0].rcp_cycle, 1315U);
    CHECK_EQ(bulk[1].rcp_cycle, 2630U);
}

TEST(audio_output_captures_the_period_used_before_a_callback_changes_the_dac) {
    auto system = std::make_unique<System>(VideoStandard::Pal);
    start_samples(*system, 1023);
    std::vector<AudioSample> samples;
    system->bus.audio_output = [&](s16, s16) { system->bus.write(0x04500010, 4, 2047); };
    system->bus.set_audio_sample_output([&](const AudioSample& sample) { samples.push_back(sample); });
    system->bus.tick(4000);
    CHECK_EQ(samples.size(), 2U);
    CHECK_EQ(samples[0].rate_numerator, 49656530U);
    CHECK_EQ(samples[0].rate_denominator, 1024U);
    CHECK_EQ(samples[1].rate_denominator, 2048U);
}

TEST(audio_output_reset_cancels_a_pending_secondary_observer_and_resets_the_clock) {
    auto system = std::make_unique<System>();
    start_samples(*system, 1023);
    unsigned legacy = 0;
    unsigned timed = 0;
    system->bus.audio_output = [&](s16, s16) {
        ++legacy;
        system->reset();
    };
    system->bus.set_audio_sample_output([&](const AudioSample&) { ++timed; });
    system->bus.tick(1315);
    CHECK_EQ(legacy, 1U);
    CHECK_EQ(timed, 0U);
    CHECK_EQ(system->bus.output_clock(), 0U);
}

TEST(audio_output_reports_silence_without_extending_the_legacy_dma_callback) {
    auto system = std::make_unique<System>();
    start_samples(*system, 1023);
    unsigned legacy = 0;
    std::vector<AudioSample> timed;
    system->bus.audio_output = [&](s16, s16) { ++legacy; };
    system->bus.set_audio_sample_output([&](const AudioSample& sample) { timed.push_back(sample); });
    system->bus.tick(4000);
    CHECK_EQ(legacy, 2U);
    CHECK_EQ(timed.size(), 3U);
    CHECK(timed[0].from_dma);
    CHECK(timed[1].from_dma);
    CHECK(!timed[2].from_dma);
    CHECK_EQ(timed[2].left, 0);
    CHECK_EQ(timed[2].right, 0);
    CHECK_EQ(timed[2].rcp_cycle, 3944U);
}
