#include "session_internal.hpp"

#include "../tasks/parallel_ranges.hpp"

#include <iostream>
#include <stdexcept>

namespace cupid::host {

Session::Impl::Impl() : worker([this](std::stop_token stop) { work(stop); }) {}

Session::Impl::~Impl() {
    worker.request_stop();
    changed.notify_all();
    worker.join();
}

void Session::Impl::work(std::stop_token stop) {
    const tasks::ParallelRangesScope render_workers;
    audio_scratch.reserve(1024);
    while (!stop.stop_requested()) {
        std::optional<Command> command;
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return stop.stop_requested() || !commands.empty() || running; });
            if (stop.stop_requested())
                break;
            if (!commands.empty()) {
                command = std::move(commands.front());
                commands.pop_front();
            }
        }
        if (command) {
            execute(std::move(*command));
            continue;
        }
        try {
            pace(stop);
            {
                std::lock_guard lock(mutex);
                if (stop.stop_requested() || !commands.empty())
                    continue;
            }
            step();
        } catch (const std::exception& error) {
            fail(error.what());
            std::lock_guard lock(mutex);
            snapshot.state = SessionState::Faulted;
        }
    }
    try {
        std::string error;
        if (!flush(error))
            std::cerr << "Session shutdown: " << error << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Session shutdown: " << error.what() << '\n';
    }
}

void Session::Impl::restart_pacing() {
    pacing_start = speed_start = std::chrono::steady_clock::now();
    pacing_cycles = speed_cycles = system ? system->cpu.cycles : 0;
}

void Session::Impl::pace(std::stop_token stop) {
    constexpr double cpu_frequency = 93750000.0;
    if (system->cpu.cycles < pacing_cycles || system->cpu.cycles < speed_cycles)
        restart_pacing();
    const auto now = std::chrono::steady_clock::now();
    const auto emulated = std::chrono::duration<double>(
        static_cast<double>(system->cpu.cycles - pacing_cycles) / cpu_frequency);
    const auto target =
        pacing_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(emulated);
    if (now - target > std::chrono::milliseconds(250)) {
        // Drop a host pacing backlog without skipping any emulated instruction.
        pacing_start = now;
        pacing_cycles = system->cpu.cycles;
        return;
    }
    if (target > now) {
        std::unique_lock lock(mutex);
        changed.wait_until(lock, target, [&] { return stop.stop_requested() || !commands.empty(); });
    }
}

void Session::Impl::step() {
    std::array<ControllerState, 4> latest;
    {
        std::lock_guard lock(mutex);
        latest = input;
    }
    for (unsigned port = 0; port < latest.size(); ++port) {
        auto controller = options.ports[port].controller;
        controller.buttons = latest[port].buttons;
        controller.stick_x = latest[port].stick_x;
        controller.stick_y = latest[port].stick_y;
        system->bus.set_controller_state(port, controller);
    }
    system->cpu.run_slice(65536, 93750);
    publish_audio();
    if (system->cpu.frozen) {
        fail("The CPU stalled on an unsupported bus transaction. Stop or reload the cartridge to continue.");
        std::lock_guard lock(mutex);
        snapshot.state = SessionState::Faulted;
        return;
    }
    publish_status();
}

void Session::Impl::attach_outputs() {
    system->bus.set_video_output([this](VideoField field) {
        auto image = video_composer.compose(std::move(field));
        if (!image)
            throw std::runtime_error("Video output contains invalid dimensions, pixels, or field parity.");
        ++fields;
        std::lock_guard lock(mutex);
        latest_video = std::move(image);
    });
    system->bus.set_audio_sample_output([this](const AudioSample& sample) {
        dma_samples += static_cast<u64>(sample.from_dma);
        resampler.append(sample, audio_scratch);
        if (audio_scratch.size() >= 512)
            publish_audio();
    });
}

void Session::Impl::clear_outputs(bool clear_video) {
    resampler.reset();
    audio_scratch.clear();
    if (clear_video)
        video_composer.reset();
    std::lock_guard lock(mutex);
    ++snapshot.output_epoch;
    audio_buffer.clear();
    if (clear_video)
        latest_video = DisplayFrame{};
}

void Session::Impl::publish_audio() {
    if (audio_scratch.empty())
        return;
    std::lock_guard lock(mutex);
    audio_buffer.append(audio_scratch);
    audio_scratch.clear();
}

void Session::Impl::publish_status() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - speed_start;
    std::lock_guard lock(mutex);
    snapshot.has_machine = system != nullptr;
    snapshot.state = running ? SessionState::Running : SessionState::Paused;
    snapshot.instructions = system->cpu.instruction_count;
    snapshot.cpu_cycles = system->cpu.cycles;
    snapshot.fields = fields;
    snapshot.dma_samples = dma_samples;
    snapshot.dropped_audio_frames = audio_buffer.dropped_frames();
    for (unsigned port = 0; port < snapshot.rumble.size(); ++port)
        snapshot.rumble[port] = system->bus.rumble_active(port);
    if (elapsed >= std::chrono::milliseconds(250)) {
        snapshot.speed_ratio = static_cast<double>(system->cpu.cycles - speed_cycles) /
                               (93750000.0 * std::chrono::duration<double>(elapsed).count());
        speed_start = now;
        speed_cycles = system->cpu.cycles;
    }
}

} // namespace cupid::host
