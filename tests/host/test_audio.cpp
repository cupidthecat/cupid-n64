#include "cupid/host/audio.hpp"
#include "test.hpp"

#include <limits>

using namespace cupid;
using namespace cupid::host;

TEST(host_audio_resampler_preserves_channels_and_interpolates_the_hardware_timeline) {
    AudioResampler resampler;
    std::vector<s16> output;
    resampler.append({0, 12000, 100}, output);
    resampler.append({12000, 0, 125100}, output);
    CHECK_EQ(output.size(), 194U);
    CHECK_EQ(output[0], 0);
    CHECK_EQ(output[1], 12000);
    CHECK_EQ(output[96], 6000);
    CHECK_EQ(output[97], 6000);
    CHECK_EQ(output[192], 12000);
    CHECK_EQ(output[193], 0);
}

TEST(host_audio_resampler_keeps_its_fraction_across_rate_changes) {
    AudioResampler resampler;
    std::vector<s16> output;
    resampler.append({100, -100, 0, 44100, 1}, output);
    for (u64 time = 1000; time <= 625000; time += 1000)
        resampler.append({100, -100, time, 62500, 1}, output);
    CHECK_EQ(output.size(), 962U);
    for (u64 time = 626250; time <= 1250000; time += 1250)
        resampler.append({100, -100, time, 50000, 1}, output);
    CHECK_EQ(output.size(), 1922U);
    for (std::size_t index = 0; index < output.size(); index += 2) {
        CHECK_EQ(output[index], 100);
        CHECK_EQ(output[index + 1], -100);
    }
}

TEST(host_audio_resampler_bounds_discontinuities_and_handles_clock_wrap) {
    AudioResampler resampler;
    std::vector<s16> output;
    resampler.append({1, -1, std::numeric_limits<u64>::max() - 62500}, output);
    resampler.append({1, -1, std::numeric_limits<u64>::max()}, output);
    resampler.append({1, -1, 62499}, output);
    CHECK_EQ(output.size(), 194U);
    CHECK_EQ(resampler.discontinuities(), 0U);
    resampler.append({2, -2, 1000000000}, output);
    CHECK_EQ(output.size(), 196U);
    CHECK_EQ(resampler.discontinuities(), 1U);
    resampler.reset();
    output.clear();
    resampler.append({3, -3, 0}, output);
    CHECK_EQ(output.size(), 2U);
}

TEST(host_audio_buffer_keeps_complete_recent_stereo_frames_with_bounded_memory) {
    AudioBuffer buffer;
    std::vector<s16> input;
    for (std::size_t index = 0; index < AudioBuffer::capacity_frames + 7; ++index) {
        input.push_back(static_cast<s16>(index));
        input.push_back(static_cast<s16>(-static_cast<s32>(index)));
    }
    buffer.append(input);
    CHECK_EQ(buffer.frames(), AudioBuffer::capacity_frames);
    CHECK_EQ(buffer.dropped_frames(), 7U);
    const auto first = buffer.take(1);
    CHECK_EQ(first.size(), 2U);
    CHECK_EQ(first[0], 7);
    CHECK_EQ(first[1], -7);
    const auto rest = buffer.take();
    CHECK_EQ(rest.back(), -static_cast<s16>(AudioBuffer::capacity_frames + 6));
    CHECK_EQ(buffer.frames(), 0U);
    buffer.clear();
    CHECK_EQ(buffer.dropped_frames(), 0U);
}
