#include "application_internal.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace cupid::desktop {

void Application::text(float x, float y, std::string_view value, bool dim) {
    sdl_check(SDL_SetRenderDrawColor(renderer.get(), dim ? 147 : 230, dim ? 157 : 235, dim ? 176 : 244, 255));
    sdl_check(SDL_RenderDebugText(renderer.get(), x, y, std::string(value).c_str()));
}

void Application::button(float x, float y, float width, std::string label, std::function<void()> action,
                         bool enabled) {
    const SDL_FRect area{x, y, width, 23};
    const bool hover = enabled && mouse_x >= x && mouse_x < x + width && mouse_y >= y && mouse_y < y + area.h;
    sdl_check(
        SDL_SetRenderDrawColor(renderer.get(), hover ? 67 : 38, hover ? 89 : 50, hover ? 124 : 72, 255));
    sdl_check(SDL_RenderFillRect(renderer.get(), &area));
    const auto columns = static_cast<std::size_t>(std::max(0.0F, (width - 12.0F) / 8.0F));
    text(x + 6, y + 7, fit_text(label, columns), !enabled);
    buttons.push_back({area, std::move(action), enabled});
}

void Application::draw_toolbar() {
    button(12, 10, 52, "Game", [this] { set_page(Page::Game); });
    button(70, 10, 84, "Hardware", [this] { set_page(Page::Hardware); });
    button(160, 10, 84, "Controls", [this] { set_page(Page::Controls); });
    button(
        250, 10, 68, status.state == host::SessionState::Running ? "Pause" : "Resume",
        [this] { pause_game(); }, status.has_machine);
    button(324, 10, 60, "Reset", [this] { static_cast<void>(request(session.reset())); }, status.has_machine);
    button(390, 10, 92, "Fullscreen", [this] { set_fullscreen(); });
    button(488, 10, 60, "Exit", [this] { request_quit(); });
}

void Application::draw_game() {
    constexpr unsigned available_width = 536;
    constexpr unsigned available_height = 296;
    const auto fitted = host::letterbox_4_3(available_width, available_height);
    const SDL_FRect destination{12 + static_cast<float>(fitted.x), 42 + static_cast<float>(fitted.y),
                                static_cast<float>(fitted.width), static_cast<float>(fitted.height)};
    sdl_check(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
    sdl_check(SDL_RenderFillRect(renderer.get(), &destination));
    std::string error;
    if (!video->draw(destination, error)) {
        message = "Video: " + error;
        exit_code = 1;
    }
    if (!status.has_machine || status.state == host::SessionState::Loading) {
        text(44, 163, state_name(status.state));
        if (status.state != host::SessionState::Loading)
            text(44, 183, "Select a cartridge and firmware on the Hardware page.", true);
    }
}

void Application::draw_footer() {
    button(12, 345, 76, preferences.muted ? "Unmute" : "Mute", [this] {
        preferences.muted = !preferences.muted;
        update_volume();
        save_settings();
    });
    button(94, 345, 28, "-", [this] {
        preferences.volume = std::max(0.0F, preferences.volume - 0.05F);
        update_volume();
        save_settings();
    });
    text(131, 353, std::to_string(static_cast<int>(std::lround(preferences.volume * 100))) + '%');
    button(174, 345, 28, "+", [this] {
        preferences.volume = std::min(1.0F, preferences.volume + 0.05F);
        update_volume();
        save_settings();
    });
    button(208, 345, 100, "Retry audio", [this] { reopen_audio(); });
    button(314, 345, 128, smoothing ? "Smooth pixels" : "Sharp pixels", [this] {
        std::string error;
        if (video->set_smoothing(!smoothing, error))
            smoothing = !smoothing;
        else
            message = "Video: " + error;
    });
    button(
        448, 345, 100, "Stop game", [this] { static_cast<void>(request(session.stop())); },
        status.has_machine);
    std::ostringstream line;
    line << state_name(status.state);
    if (status.state == host::SessionState::Running) {
        line << " | Speed " << std::fixed << std::setprecision(0) << status.speed_ratio * 100.0 << '%';
        if (status.speed_ratio < 0.95 && status.cpu_cycles >= 93750000)
            line << " (host below full speed)";
    }
    if (!audio.status().available)
        line << " | Audio unavailable: Retry audio";
    text(12, 377, fit_text(line.str(), 67), true);
    text(12, 397, fit_text(message, 67));
}

void Application::draw_quit_failure() {
    buttons.clear();
    const SDL_FRect panel{20, 112, 520, 166};
    sdl_check(SDL_SetRenderDrawColor(renderer.get(), 29, 37, 53, 255));
    sdl_check(SDL_RenderFillRect(renderer.get(), &panel));
    text(36, 130, "Some changes could not be saved.");
    text(36, 154, fit_text(message, 61));
    text(36, 181, "The previous saved files remain available.", true);
    button(36, 224, 106, "Retry save", [this] { request_quit(); });
    button(154, 224, 112, "Stay open", [this] {
        quit_failed = false;
        update_focus();
    });
    button(278, 224, 242, "Exit without saving", [this] { request_quit(true); });
}

void Application::draw_binding_capture() {
    buttons.clear();
    const SDL_FRect panel{36, 124, 488, 156};
    sdl_check(SDL_SetRenderDrawColor(renderer.get(), 29, 37, 53, 255));
    sdl_check(SDL_RenderFillRect(renderer.get(), &panel));
    const auto capture = *binding_capture;
    text(52, 142, "Binding controller port " + std::to_string(capture.port + 1));
    text(52, 168,
         capture.keyboard ? "Press a key. Backspace clears this binding."
                          : "Press a gamepad button or move the desired axis.");
    text(52, 192, "Escape cancels. Changes are saved immediately.", true);
    button(52, 230, 104, "Cancel", [this] {
        binding_capture.reset();
        update_focus();
    });
}

void Application::draw() {
    sdl_check(SDL_SetRenderLogicalPresentation(renderer.get(), 560, 420, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    sdl_check(SDL_SetRenderDrawColor(renderer.get(), 17, 23, 34, 255));
    sdl_check(SDL_RenderClear(renderer.get()));
    buttons.clear();
    draw_toolbar();
    switch (page) {
    case Page::Game:
        draw_game();
        break;
    case Page::Hardware:
        draw_hardware();
        break;
    case Page::Controls:
        draw_controls();
        break;
    }
    draw_footer();
    if (binding_capture)
        draw_binding_capture();
    if (quit_failed)
        draw_quit_failure();
    if (quitting)
        buttons.clear();
    if (screenshot_requested) {
        capture_frame();
        screenshot_requested = false;
    }
    sdl_check(SDL_RenderPresent(renderer.get()));
    if (timed_expired && !quitting && !quit_failed)
        request_quit();
}

} // namespace cupid::desktop
