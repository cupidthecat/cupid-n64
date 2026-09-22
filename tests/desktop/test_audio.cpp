#include "cupid/desktop/audio.hpp"
#include "test.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace cupid;
using namespace cupid::desktop;

namespace {

std::optional<std::string> environment_value(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    CHECK(_dupenv_s(&value, &length, name) == 0);
    const std::unique_ptr<char, decltype(&std::free)> owned(value, std::free);
    return value ? std::optional<std::string>{value} : std::nullopt;
#else
    const char* value = std::getenv(name);
    return value ? std::optional<std::string>{value} : std::nullopt;
#endif
}

class ScopedEnvironment {
  public:
    ScopedEnvironment(std::string name, std::optional<std::string_view> value) : name_(std::move(name)) {
        old_ = environment_value(name_.c_str());
        set(value);
    }

    ~ScopedEnvironment() {
        if (old_) {
            set(*old_);
        } else {
            set(std::nullopt);
        }
    }

    void set(std::optional<std::string_view> value) {
#ifdef _WIN32
        const std::string text = value ? std::string(*value) : std::string{};
        CHECK(_putenv_s(name_.c_str(), text.c_str()) == 0);
#else
        if (value) {
            CHECK(setenv(name_.c_str(), std::string(*value).c_str(), 1) == 0);
        } else {
            CHECK(unsetenv(name_.c_str()) == 0);
        }
#endif
    }

  private:
    std::string name_;
    std::optional<std::string> old_;
};

struct DummyAudio {
    ScopedEnvironment driver{"SDL_AUDIO_DRIVER", "dummy"};
    ScopedEnvironment timescale{"SDL_AUDIO_DUMMY_TIMESCALE", "1"};
};

std::vector<s16> silence(std::size_t frames) {
    return std::vector<s16>(frames * AudioOutput::channels);
}

} // namespace

TEST(audio_open_failure_is_actionable) {
    AudioOutput output;
    std::string error;

    CHECK(!output.open(error));
    CHECK(!error.empty());
    CHECK(!output.status().open);
    CHECK(!output.status().available);
    CHECK(!output.status().error.empty());
}

TEST(desktop_audio_dummy_device_opens_paused_and_closes_cleanly) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;

    CHECK(output.open(error));
    const auto opened = output.status();
    CHECK(opened.open);
    CHECK(opened.available);
    CHECK(!opened.running);
    CHECK(opened.error.empty());

    output.close();
    const auto closed = output.status();
    CHECK(!closed.open);
    CHECK(!closed.available);
    CHECK(!closed.running);
    CHECK_EQ(closed.queued_frames, std::size_t{0});
}

TEST(desktop_audio_prefills_before_start_and_recovers_after_underrun) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;
    CHECK(output.open(error));
    CHECK(output.set_running(true, error));
    CHECK(!output.status().running);

    const auto first = silence(AudioOutput::prefill_frames - 1);
    CHECK(output.submit(first, error));
    CHECK(!output.status().running);
    const auto final_frame = silence(1);
    CHECK(output.submit(final_frame, error));
    CHECK(output.status().running);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(output.submit(final_frame, error));
    const auto recovered = output.status();
    CHECK_EQ(recovered.underruns, std::size_t{1});
    CHECK(!recovered.running);
    CHECK_EQ(recovered.queued_frames, std::size_t{1});

    const auto refill = silence(AudioOutput::prefill_frames - 1);
    CHECK(output.submit(refill, error));
    CHECK(output.status().running);
}

TEST(desktop_audio_set_running_true_is_idempotent_while_playing_below_prefill) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;
    CHECK(output.open(error));
    CHECK(output.submit(silence(AudioOutput::prefill_frames + 480), error));
    CHECK(output.set_running(true, error));
    CHECK(output.status().running);

    bool below_prefill = false;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto status = output.status();
        if (status.queued_frames != 0 && status.queued_frames < AudioOutput::prefill_frames) {
            below_prefill = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(below_prefill);
    CHECK(output.set_running(true, error));
    CHECK(output.status().running);
}

TEST(desktop_audio_queue_is_bounded_to_100ms_and_counts_dropped_frames) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;
    CHECK(output.open(error));

    constexpr std::size_t excess = 137;
    const auto samples = silence(AudioOutput::max_queue_frames + excess);
    CHECK(output.submit(samples, error));
    const auto status = output.status();
    CHECK_EQ(status.queued_frames, AudioOutput::max_queue_frames);
    CHECK_EQ(status.dropped_frames, excess);
    CHECK(!status.running);
}

TEST(desktop_audio_gain_and_mute_do_not_accumulate_and_clear_drops_queue) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;

    CHECK(output.set_volume(0.25F, false, error));
    CHECK(output.open(error));
    CHECK(std::abs(output.status().gain - 0.25F) < 0.0001F);
    CHECK(!output.status().muted);

    CHECK(output.set_volume(0.75F, true, error));
    CHECK(output.status().muted);
    CHECK(std::abs(output.status().gain - 0.75F) < 0.0001F);
    CHECK(output.set_volume(0.75F, false, error));
    CHECK(!output.status().muted);
    CHECK(std::abs(output.status().gain - 0.75F) < 0.0001F);

    const auto queued = silence(256);
    CHECK(output.submit(queued, error));
    CHECK_EQ(output.status().queued_frames, std::size_t{256});
    output.clear();
    CHECK_EQ(output.status().queued_frames, std::size_t{0});
    CHECK(!output.status().running);
}

TEST(desktop_audio_pause_clears_queue_and_reopen_restores_a_device) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;
    CHECK(output.open(error));
    CHECK(output.submit(silence(300), error));
    CHECK_EQ(output.status().queued_frames, std::size_t{300});

    CHECK(output.set_running(false, error));
    CHECK_EQ(output.status().queued_frames, std::size_t{0});
    CHECK(!output.status().running);
    output.close();
    CHECK(output.open(error));
    CHECK(output.status().available);
}

TEST(desktop_audio_move_assignment_releases_destination_sdl_reference) {
    DummyAudio environment;
    CHECK_EQ(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO, 0U);
    {
        AudioOutput source;
        AudioOutput destination;
        std::string error;
        CHECK(source.open(error));
        CHECK(destination.open(error));
        CHECK((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0U);
        destination = std::move(source);
        destination.close();
    }
    CHECK_EQ(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO, 0U);
}

TEST(desktop_audio_rejects_partial_stereo_frames_and_invalid_gain) {
    DummyAudio environment;
    AudioOutput output;
    std::string error;
    CHECK(output.open(error));

    const std::vector<s16> incomplete{1};
    CHECK(!output.submit(incomplete, error));
    CHECK(error.find("stereo") != std::string::npos);
    CHECK(!output.set_volume(-0.1F, false, error));
    CHECK(error.find("non-negative") != std::string::npos);
}

TEST(desktop_audio_native_device_open_smoke) {
    const auto enabled = environment_value("CUPID_AUDIO_NATIVE_SMOKE");
    if (!enabled || *enabled != "1")
        return;

    ScopedEnvironment driver{"SDL_AUDIO_DRIVER", std::nullopt};
    ScopedEnvironment timescale{"SDL_AUDIO_DUMMY_TIMESCALE", std::nullopt};
    AudioOutput output;
    std::string error;
    CHECK(output.open(error));
    CHECK(output.status().available);
    output.close();
}
