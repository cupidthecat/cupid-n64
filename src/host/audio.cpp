#include "cupid/host/audio.hpp"

#include <algorithm>
#include <stdexcept>

namespace cupid::host {
namespace {

constexpr u64 rcp_frequency = 62500000;
constexpr u64 maximum_gap = rcp_frequency / 4;

s16 interpolate(s16 previous, s16 current, u64 position, u64 interval) {
    const s64 difference = static_cast<s64>(current) - previous;
    const s64 value =
        static_cast<s64>(previous) + (difference * static_cast<s64>(position)) / static_cast<s64>(interval);
    return static_cast<s16>(value);
}

} // namespace

void AudioResampler::reset() {
    previous_ = {};
    fraction_ = 0;
    discontinuities_ = 0;
    started_ = false;
}

void AudioResampler::append(const AudioSample& sample, std::vector<s16>& interleaved) {
    const auto restart = [&] {
        previous_ = sample;
        fraction_ = 0;
        started_ = true;
        interleaved.push_back(sample.left);
        interleaved.push_back(sample.right);
    };
    if (!started_) {
        restart();
        return;
    }
    const u64 elapsed = sample.rcp_cycle - previous_.rcp_cycle;
    if (elapsed > maximum_gap) {
        ++discontinuities_;
        restart();
        return;
    }
    if (elapsed == 0) {
        previous_ = sample;
        return;
    }

    const u64 interval = elapsed * playback_frequency;
    const u64 total = interval + fraction_;
    const u64 count = total / rcp_frequency;
    for (u64 index = 1; index <= count; ++index) {
        const u64 position = index * rcp_frequency - fraction_;
        interleaved.push_back(interpolate(previous_.left, sample.left, position, interval));
        interleaved.push_back(interpolate(previous_.right, sample.right, position, interval));
    }
    fraction_ = total % rcp_frequency;
    previous_ = sample;
}

void AudioBuffer::clear() {
    first_ = 0;
    size_ = 0;
    dropped_ = 0;
}

void AudioBuffer::append(std::span<const s16> interleaved) {
    if ((interleaved.size() & 1U) != 0)
        throw std::invalid_argument("Stereo audio must contain complete left/right pairs.");
    for (std::size_t index = 0; index < interleaved.size(); index += 2) {
        if (size_ == capacity_frames) {
            first_ = (first_ + 1) % capacity_frames;
            --size_;
            ++dropped_;
        }
        samples_[(first_ + size_) % capacity_frames] = {interleaved[index], interleaved[index + 1]};
        ++size_;
    }
}

std::vector<s16> AudioBuffer::take(std::size_t max_frames) {
    const std::size_t count = std::min(max_frames, size_);
    std::vector<s16> output;
    output.reserve(count * 2);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& sample = samples_[(first_ + index) % capacity_frames];
        output.push_back(sample[0]);
        output.push_back(sample[1]);
    }
    first_ = (first_ + count) % capacity_frames;
    size_ -= count;
    return output;
}

} // namespace cupid::host
