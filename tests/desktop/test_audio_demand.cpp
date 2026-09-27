#include "cupid/desktop/audio.hpp"
#include "test.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <thread>
#include <vector>

struct SDL_AudioStream {
    SDL_AudioStreamCallback callback{};
    void* userdata{};
    std::vector<cupid::s16> queued;
    bool paused = true;
};

namespace {
using namespace cupid;
using desktop::AudioOutput;
SDL_AudioStream* device{};
unsigned pauses{};
unsigned resumes{};

// Consume samples at explicit device requests, independent of frontend polling.
std::vector<s16> pull(std::size_t frames) {
    CHECK(device != nullptr);
    CHECK(!device->paused);
    const auto count = frames * AudioOutput::channels;
    const int total = static_cast<int>(count * sizeof(s16));
    const int missing = static_cast<int>((count - std::min(count, device->queued.size())) * sizeof(s16));
    if (device->callback)
        device->callback(device->userdata, device, missing, total);
    const auto taken = std::min(count, device->queued.size());
    std::vector<s16> output(device->queued.begin(),
                            device->queued.begin() + static_cast<std::ptrdiff_t>(taken));
    device->queued.erase(device->queued.begin(), device->queued.begin() + static_cast<std::ptrdiff_t>(taken));
    return output;
}

std::vector<s16> samples(std::size_t frames, s16 value = 0) {
    return std::vector<s16>(frames * AudioOutput::channels, value);
}

struct Playback {
    AudioOutput output;
    std::string error;
    Playback() {
        CHECK(device == nullptr);
        pauses = resumes = 0;
        CHECK(output.open(error));
        CHECK(output.set_running(true, error));
    }
    void start() {
        CHECK(output.submit(samples(AudioOutput::prefill_frames, 42), error));
        CHECK(output.status().running);
    }
};
} // namespace

extern "C" {
const char* SDLCALL SDL_GetError() {
    return "scripted audio error";
}
bool SDLCALL SDL_InitSubSystem(SDL_InitFlags) {
    return true;
}
void SDLCALL SDL_QuitSubSystem(SDL_InitFlags) {}
SDL_AudioStream* SDLCALL SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec*,
                                                   SDL_AudioStreamCallback callback, void* userdata) {
    device = new SDL_AudioStream;
    device->callback = callback;
    device->userdata = userdata;
    return device;
}
SDL_AudioDeviceID SDLCALL SDL_GetAudioStreamDevice(SDL_AudioStream*) {
    return 1;
}
int SDLCALL SDL_GetAudioStreamQueued(SDL_AudioStream* stream) {
    return static_cast<int>(stream->queued.size() * sizeof(s16));
}
bool SDLCALL SDL_PauseAudioStreamDevice(SDL_AudioStream* stream) {
    ++pauses;
    stream->paused = true;
    // An in-flight device callback can finish while pause waits on the stream.
    if (stream->callback)
        stream->callback(stream->userdata, stream, 4, 4);
    return true;
}
bool SDLCALL SDL_ResumeAudioStreamDevice(SDL_AudioStream* stream) {
    ++resumes;
    stream->paused = false;
    return true;
}
void SDLCALL SDL_DestroyAudioStream(SDL_AudioStream* stream) {
    if (stream->callback)
        stream->callback(stream->userdata, stream, 4, 4);
    device = nullptr;
    delete stream;
}
bool SDLCALL SDL_SetAudioStreamGain(SDL_AudioStream*, float) {
    return true;
}
bool SDLCALL SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* data, int bytes) {
    const auto* first = static_cast<const s16*>(data);
    stream->queued.insert(stream->queued.end(), first, first + bytes / static_cast<int>(sizeof(s16)));
    return true;
}
bool SDLCALL SDL_ClearAudioStream(SDL_AudioStream* stream) {
    stream->queued.clear();
    return true;
}
}

TEST(desktop_audio_demand_empty_queue_after_complete_pull_keeps_playing) {
    Playback playback;
    playback.start();
    CHECK_EQ(pull(AudioOutput::prefill_frames), samples(AudioOutput::prefill_frames, 42));
    CHECK_EQ(playback.output.status().queued_frames, 0U);
    CHECK(playback.output.submit(samples(1, 1234), playback.error));
    CHECK_EQ(playback.output.status().underruns, 0U);
    CHECK(playback.output.status().running);
    CHECK_EQ(pauses, 0U);
    CHECK_EQ(resumes, 1U);
    CHECK_EQ(pull(1), samples(1, 1234));
}

TEST(desktop_audio_demand_short_pull_counts_without_a_producer_poll) {
    Playback playback;
    playback.start();
    CHECK_EQ(pull(AudioOutput::prefill_frames + 1).size(), AudioOutput::prefill_frames * 2);
    CHECK_EQ(playback.output.status().underruns, 1U);
    CHECK(pull(1).empty());
    CHECK_EQ(playback.output.status().underruns, 1U);
    CHECK(playback.output.submit(samples(1, -2345), playback.error));
    CHECK(playback.output.status().running);
    CHECK_EQ(pull(1), samples(1, -2345));
    CHECK(pull(1).empty());
    CHECK_EQ(playback.output.status().underruns, 2U);
    CHECK_EQ(pauses, 0U);
}

TEST(desktop_audio_demand_polling_does_not_create_or_repeat_a_shortage) {
    Playback playback;
    playback.start();
    pull(AudioOutput::prefill_frames);
    for (unsigned index = 0; index < 8; ++index) {
        CHECK(playback.output.submit({}, playback.error));
        CHECK_EQ(playback.output.status().underruns, 0U);
        CHECK(playback.output.status().running);
    }
    CHECK(pull(1).empty());
    CHECK_EQ(playback.output.status().underruns, 1U);
    for (unsigned index = 0; index < 8; ++index)
        CHECK(playback.output.submit({}, playback.error));
    CHECK_EQ(playback.output.status().underruns, 1U);
    CHECK_EQ(pauses, 0U);
}

TEST(desktop_audio_demand_pause_clear_and_close_ignore_inflight_callbacks) {
    Playback playback;
    playback.start();
    CHECK(playback.output.set_running(false, playback.error));
    CHECK_EQ(playback.output.status().underruns, 0U);
    CHECK_EQ(playback.output.status().queued_frames, 0U);
    CHECK(!playback.output.status().running);
    CHECK(playback.output.set_running(true, playback.error));
    CHECK(playback.output.submit(samples(AudioOutput::prefill_frames - 1), playback.error));
    CHECK(!playback.output.status().running);
    CHECK(playback.output.submit(samples(1), playback.error));
    CHECK(playback.output.status().running);
    playback.output.clear();
    CHECK(!playback.output.status().running);
    CHECK_EQ(playback.output.status().underruns, 0U);
    CHECK_EQ(playback.output.status().queued_frames, 0U);
    playback.output.close();
    CHECK_EQ(playback.output.status().underruns, 0U);
    CHECK(!playback.output.status().open);
}

TEST(desktop_audio_demand_callback_context_survives_move_and_cross_thread_requests) {
    Playback playback;
    playback.start();
    AudioOutput moved(std::move(playback.output));
    std::exception_ptr failure;
    std::thread consumer([&] {
        try {
            pull(AudioOutput::prefill_frames + 1);
        } catch (...) {
            failure = std::current_exception();
        }
    });
    consumer.join();
    if (failure)
        std::rethrow_exception(failure);
    CHECK_EQ(moved.status().underruns, 1U);
    CHECK(moved.submit(samples(1, 123), playback.error));
    CHECK_EQ(pull(1), samples(1, 123));
    CHECK(moved.status().running);
    moved.close();
    CHECK_EQ(moved.status().underruns, 1U);
}
