#include "application_internal.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace cupid::desktop {
namespace {

u64 frame_hash(const host::DisplayFrame& frame) {
    u64 hash = 14695981039346656037ULL;
    for (const auto pixel : frame.pixels) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= (pixel >> shift) & 0xffU;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

} // namespace

void Application::update() {
    if (auto selection = dialogs.take_result())
        file_selected(std::move(*selection));
    status = session.status();
    update_focus();
    input.poll();
    session.set_input(mapper.controller_states());
    input.set_rumble(status.rumble);
    auto output = session.take_output();
    if (output.epoch != output_epoch) {
        output_epoch = output.epoch;
        audio.clear();
    }
    if (output.video) {
        std::string error;
        if (!video->update(*output.video, error)) {
            message = "Video: " + error;
            exit_code = 1;
        } else if (!output.video->pixels.empty()) {
            ++displayed_frames;
            last_frame_hash = frame_hash(*output.video);
        }
    }
    const bool running = status.state == host::SessionState::Running && !quitting;
    auto audio_status = audio.status();
    std::string error;
    if (audio_status.open && running != audio_running) {
        if (!audio.set_running(running, error))
            message = "Audio: " + error;
        else
            audio_running = running;
    }
    if (running && audio_status.open && !output.audio.empty() && !audio.submit(output.audio, error))
        message = "Audio: " + error;
    audio_status = audio.status();
    if (!audio_status.error.empty() && audio_status.error != last_audio_error) {
        last_audio_error = audio_status.error;
        message = "Audio: " + last_audio_error;
    }
    if (!status.error.empty() && status.error != last_session_error) {
        last_session_error = status.error;
        message = status.error;
        std::cerr << "Session: " << status.error << '\n';
    } else if (status.error.empty()) {
        last_session_error.clear();
    }
    if (status.has_machine) {
        last_run_status = status;
        last_audio_status = audio_status;
    }
    const auto now = SDL_GetTicks();
    if (load_request != 0 && status.completed_request >= load_request) {
        load_request = 0;
        if (status.error.empty() && status.has_machine) {
            run_started = now;
            save_settings();
        } else if (launch.run_seconds != 0) {
            exit_code = 1;
            request_quit();
        }
    }
    if (launch.run_seconds != 0 && !timed_expired && run_started &&
        static_cast<double>(now - *run_started) >= launch.run_seconds * 1000.0) {
        timed_expired = true;
        screenshot_requested = !launch.capture.empty();
        if (!launch.paused && displayed_frames == 0) {
            exit_code = 1;
            std::cerr << "The timed run produced no nonblank video fields.\n";
        }
    }
    if (launch.run_seconds != 0 && status.state == host::SessionState::Faulted && !quitting) {
        exit_code = 1;
        timed_expired = true;
        screenshot_requested = !launch.capture.empty();
    }
    if (quitting && status.completed_request >= quit_request) {
        if (status.has_machine) {
            quitting = false;
            quit_failed = true;
            message = status.error;
        } else {
            preferences.input_bindings = host::encode_input_bindings(mapper.bindings());
            if (!quit_discard && !host::save_preferences(preferences_path, preferences, error)) {
                quitting = false;
                quit_failed = true;
                message = "Settings were not saved: " + error;
            } else {
                report_run();
                finished = true;
            }
        }
        if (quit_failed && launch.run_seconds != 0) {
            std::cerr << message << '\n';
            exit_code = 1;
            report_run();
            finished = true;
        }
    }
    std::ostringstream title;
    title << "Cupid-N64 | " << state_name(status.state);
    if (status.state == host::SessionState::Running)
        title << " | " << std::fixed << std::setprecision(0) << status.speed_ratio * 100.0 << '%';
    const auto next_title = title.str();
    if (last_title != next_title) {
        sdl_check(SDL_SetWindowTitle(window.get(), next_title.c_str()));
        last_title = next_title;
    }
}

void Application::report_run() const {
    if (launch.run_seconds == 0)
        return;
    const double seconds = run_started ? static_cast<double>(SDL_GetTicks() - *run_started) / 1000.0 : 0.0;
    std::cout << "Desktop run: wall_seconds=" << seconds << " instructions=" << last_run_status.instructions
              << " cpu_cycles=" << last_run_status.cpu_cycles << " fields=" << last_run_status.fields
              << " presented=" << displayed_frames << " dma_samples=" << last_run_status.dma_samples
              << " host_dropped_audio=" << last_run_status.dropped_audio_frames
              << " device_dropped_audio=" << last_audio_status.dropped_frames
              << " queued_audio_frames=" << last_audio_status.queued_frames
              << " audio_underruns=" << last_audio_status.underruns << " frame_hash=" << std::hex
              << last_frame_hash << std::dec << " exit_code=" << exit_code << '\n';
}

} // namespace cupid::desktop
