#include "cupid/desktop/audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <string_view>

namespace cupid::desktop {
namespace {

constexpr std::size_t bytes_per_frame = AudioOutput::channels * sizeof(s16);

std::string sdl_error(std::string_view action) {
    const char* detail = SDL_GetError();
    std::string result(action);
    if (detail != nullptr && *detail != '\0') {
        result += ": ";
        result += detail;
    }
    return result;
}

} // namespace

struct AudioOutput::Impl {
    SDL_AudioStream* stream{};
    bool initialized{};
    bool running_requested{};
    bool running{};
    bool muted{};
    std::atomic<bool> observe_demand{};
    std::atomic<bool> starved{};
    float gain{1.0F};
    std::size_t dropped_frames{};
    std::atomic<std::size_t> underruns{};
    std::string last_error;
    mutable std::mutex mutex;

    static void SDLCALL requested(void* userdata, SDL_AudioStream*, int additional, int total) {
        auto& self = *static_cast<Impl*>(userdata);
        // SDL holds its stream lock here. Avoid the frontend mutex and observe
        // demand before the device consumes the available samples.
        if (!self.observe_demand.load(std::memory_order_relaxed))
            return;
        const bool missing = additional > 0 && total > 0;
        const bool previously_missing = self.starved.exchange(missing, std::memory_order_relaxed);
        if (missing && !previously_missing)
            self.underruns.fetch_add(1, std::memory_order_relaxed);
    }

    ~Impl() {
        destroy_stream();
        release_sdl();
    }

    bool fail(std::string message, std::string& error) {
        last_error = std::move(message);
        error = last_error;
        return false;
    }

    bool fail_sdl(std::string_view action, std::string& error) {
        return fail(sdl_error(action), error);
    }

    bool available() const {
        return stream != nullptr && SDL_GetAudioStreamDevice(stream) != 0;
    }

    bool require_available(std::string& error) {
        if (stream == nullptr)
            return fail("audio output is not open", error);
        if (SDL_GetAudioStreamDevice(stream) == 0) {
            observe_demand.store(false, std::memory_order_relaxed);
            running = false;
            return fail("audio device is unavailable; call open() to reopen", error);
        }
        return true;
    }

    bool queued_frames(std::size_t& frames, std::string& error) {
        const int queued = SDL_GetAudioStreamQueued(stream);
        if (queued < 0)
            return fail_sdl("query audio queue", error);
        frames = static_cast<std::size_t>(queued) / bytes_per_frame;
        return true;
    }

    bool pause(std::string& error) {
        observe_demand.store(false, std::memory_order_relaxed);
        if (!SDL_PauseAudioStreamDevice(stream)) {
            observe_demand.store(running, std::memory_order_relaxed);
            return fail_sdl("pause audio device", error);
        }
        starved.store(false, std::memory_order_relaxed);
        running = false;
        return true;
    }

    bool resume(std::string& error) {
        starved.store(false, std::memory_order_relaxed);
        observe_demand.store(true, std::memory_order_relaxed);
        if (!SDL_ResumeAudioStreamDevice(stream)) {
            observe_demand.store(false, std::memory_order_relaxed);
            return fail_sdl("resume audio device", error);
        }
        running = true;
        return true;
    }

    void destroy_stream() {
        observe_demand.store(false, std::memory_order_relaxed);
        if (stream != nullptr) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        running = false;
    }

    void release_sdl() {
        if (initialized) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            initialized = false;
        }
    }
};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    close();
}

AudioOutput::AudioOutput(AudioOutput&&) noexcept = default;

AudioOutput& AudioOutput::operator=(AudioOutput&&) noexcept = default;

bool AudioOutput::open(std::string& error) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    std::lock_guard lock(impl_->mutex);
    error.clear();

    if (impl_->stream != nullptr && impl_->available()) {
        impl_->last_error.clear();
        return true;
    }
    impl_->destroy_stream();

    if (!impl_->initialized) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return impl_->fail_sdl("initialize SDL audio", error);
        impl_->initialized = true;
    }

    SDL_AudioSpec input{};
    input.format = SDL_AUDIO_S16;
    input.channels = static_cast<int>(channels);
    input.freq = static_cast<int>(sample_rate);
    impl_->stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &input, &Impl::requested, impl_.get());
    if (impl_->stream == nullptr) {
        impl_->release_sdl();
        return impl_->fail_sdl("open default playback device", error);
    }

    if (!SDL_SetAudioStreamGain(impl_->stream, impl_->muted ? 0.0F : impl_->gain)) {
        const std::string message = sdl_error("set audio stream gain");
        impl_->destroy_stream();
        impl_->release_sdl();
        return impl_->fail(message, error);
    }

    impl_->running = false;
    impl_->starved.store(false, std::memory_order_relaxed);
    impl_->last_error.clear();
    return true;
}

bool AudioOutput::submit(std::span<const s16> samples, std::string& error) {
    if (!impl_) {
        error = "audio output is not open";
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    error.clear();
    if (!impl_->require_available(error))
        return false;
    if ((samples.size() % channels) != 0)
        return impl_->fail("audio sample count must contain complete stereo frames", error);

    std::size_t queued = 0;
    if (!impl_->queued_frames(queued, error))
        return false;

    const std::size_t frames = samples.size() / channels;
    const std::size_t room = queued < max_queue_frames ? max_queue_frames - queued : 0;
    const std::size_t accepted = std::min(frames, room);
    impl_->dropped_frames += frames - accepted;
    if (accepted != 0) {
        const std::size_t sample_count = accepted * channels;
        const std::size_t byte_count = sample_count * sizeof(s16);
        if (byte_count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return impl_->fail("audio submission exceeds SDL byte-count range", error);
        if (!SDL_PutAudioStreamData(impl_->stream, samples.data(), static_cast<int>(byte_count)))
            return impl_->fail_sdl("queue audio samples", error);
    }

    if (!impl_->queued_frames(queued, error))
        return false;
    if (impl_->running_requested && !impl_->running && queued >= prefill_frames) {
        if (!impl_->resume(error))
            return false;
    }

    impl_->last_error.clear();
    return true;
}

bool AudioOutput::set_running(bool running, std::string& error) {
    if (!impl_) {
        error = "audio output is not open";
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    error.clear();
    if (!impl_->require_available(error))
        return false;

    if (!running) {
        impl_->running_requested = false;
        if (!impl_->pause(error))
            return false;
        if (!SDL_ClearAudioStream(impl_->stream))
            return impl_->fail_sdl("clear paused audio stream", error);
        impl_->last_error.clear();
        return true;
    }

    impl_->running_requested = true;
    if (impl_->running) {
        impl_->last_error.clear();
        return true;
    }
    std::size_t queued = 0;
    if (!impl_->queued_frames(queued, error))
        return false;
    if (queued >= prefill_frames) {
        if (!impl_->resume(error))
            return false;
    }
    impl_->last_error.clear();
    return true;
}

bool AudioOutput::set_volume(float gain, bool mute, std::string& error) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    std::lock_guard lock(impl_->mutex);
    error.clear();
    if (!std::isfinite(gain) || gain < 0.0F)
        return impl_->fail("audio gain must be finite and non-negative", error);

    if (impl_->stream != nullptr) {
        if (!impl_->require_available(error))
            return false;
        if (!SDL_SetAudioStreamGain(impl_->stream, mute ? 0.0F : gain))
            return impl_->fail_sdl("set audio stream gain", error);
    }
    impl_->gain = gain;
    impl_->muted = mute;
    impl_->last_error.clear();
    return true;
}

void AudioOutput::clear() {
    if (!impl_)
        return;
    std::lock_guard lock(impl_->mutex);
    if (impl_->stream == nullptr)
        return;
    impl_->observe_demand.store(false, std::memory_order_relaxed);
    impl_->starved.store(false, std::memory_order_relaxed);
    if (impl_->running && !SDL_PauseAudioStreamDevice(impl_->stream))
        impl_->last_error = sdl_error("pause audio device before clearing");
    impl_->running = false;
    if (!SDL_ClearAudioStream(impl_->stream))
        impl_->last_error = sdl_error("clear audio stream");
}

void AudioOutput::close() {
    if (!impl_)
        return;
    std::lock_guard lock(impl_->mutex);
    impl_->destroy_stream();
    impl_->release_sdl();
    impl_->running_requested = false;
    impl_->starved.store(false, std::memory_order_relaxed);
}

AudioStatus AudioOutput::status() const {
    if (!impl_)
        return {};
    std::lock_guard lock(impl_->mutex);
    AudioStatus result;
    result.open = impl_->stream != nullptr;
    result.available = impl_->available();
    result.running = impl_->running && result.available;
    result.muted = impl_->muted;
    result.dropped_frames = impl_->dropped_frames;
    result.underruns = impl_->underruns.load(std::memory_order_relaxed);
    result.gain = impl_->gain;
    result.error = impl_->last_error;
    if (impl_->stream != nullptr) {
        const int queued = SDL_GetAudioStreamQueued(impl_->stream);
        if (queued >= 0) {
            result.queued_frames = static_cast<std::size_t>(queued) / bytes_per_frame;
        } else if (result.error.empty()) {
            result.error = sdl_error("query audio queue");
        }
        if (!result.available && result.error.empty())
            result.error = "audio device is unavailable; call open() to reopen";
    }
    return result;
}

} // namespace cupid::desktop
