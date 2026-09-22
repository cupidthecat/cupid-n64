#pragma once

#include "cupid/host/audio.hpp"
#include "cupid/host/session.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace cupid::host {

struct Session::Impl {
    enum class Action { Load, Pause, Reset, Stop };
    struct Command {
        Action action;
        u64 request{};
        Options options;
        bool value{};
        std::filesystem::path storage_root;
    };

    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    std::deque<Command> commands;
    std::array<ControllerState, 4> input{};
    SessionStatus snapshot;
    std::optional<DisplayFrame> latest_video;
    AudioBuffer audio_buffer;
    u64 next_request{1};

    std::unique_ptr<System> system;
    Options options;
    FrameComposer video_composer;
    AudioResampler resampler;
    std::vector<s16> audio_scratch;
    bool running{};
    u64 fields{};
    u64 dma_samples{};
    std::chrono::steady_clock::time_point pacing_start;
    std::chrono::steady_clock::time_point speed_start;
    u64 pacing_cycles{};
    u64 speed_cycles{};
    std::jthread worker;

    Impl();
    ~Impl();
    u64 enqueue(Action action, Options options = {}, bool value = false,
                std::filesystem::path storage_root = {});
    void work(std::stop_token stop);
    void execute(Command command);
    void load_machine(Options selected, bool paused, const std::filesystem::path& storage_root);
    bool flush(std::string& error);
    void attach_outputs();
    void clear_outputs(bool clear_video);
    void publish_audio();
    void publish_status();
    void fail(std::string error);
    void pace(std::stop_token stop);
    void step();
    void restart_pacing();
};

} // namespace cupid::host
