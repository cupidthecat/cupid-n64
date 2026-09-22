#pragma once

#include "cupid/host/options.hpp"
#include "cupid/host/video.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cupid::host {

enum class SessionState { Empty, Loading, Running, Paused, Faulted };

struct SessionStatus {
    SessionState state{SessionState::Empty};
    bool has_machine{};
    u64 completed_request{};
    u64 output_epoch{};
    u64 instructions{};
    u64 cpu_cycles{};
    u64 fields{};
    u64 dma_samples{};
    u64 dropped_audio_frames{};
    double speed_ratio{};
    std::array<bool, 4> rumble{};
    std::string cartridge;
    std::string hardware;
    std::string error;
};

struct SessionOutput {
    u64 epoch{};
    std::optional<DisplayFrame> video;
    std::vector<s16> audio;
};

class Session {
  public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Requests return zero when the bounded command queue is full.
    [[nodiscard]] u64 load(Options options, bool paused = false, std::filesystem::path storage_root = {});
    [[nodiscard]] u64 pause(bool paused);
    [[nodiscard]] u64 reset();
    [[nodiscard]] u64 stop(bool discard_unsaved = false);
    void set_input(const std::array<ControllerState, 4>& input);
    [[nodiscard]] SessionStatus status() const;
    [[nodiscard]] SessionOutput take_output();
    [[nodiscard]] bool wait(u64 request, std::chrono::milliseconds timeout) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cupid::host
