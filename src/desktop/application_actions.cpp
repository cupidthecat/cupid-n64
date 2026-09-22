#include "application_internal.hpp"

#include <chrono>
#include <iostream>

namespace cupid::desktop {

bool Application::request(u64 id) {
    if (id != 0)
        return true;
    message = "The current operation is still finishing. Please try again.";
    return false;
}

void Application::set_page(Page selected) {
    if (selected != Page::Game && status.state == host::SessionState::Running)
        static_cast<void>(request(session.pause(true)));
    page = selected;
    binding_capture.reset();
    update_focus();
}

void Application::update_focus() {
    input.set_focused(focused && page == Page::Game && status.state == host::SessionState::Running &&
                      !dialogs.pending() && !binding_capture && !quitting && !quit_failed);
}

void Application::open_file(FileKind kind, unsigned tag) {
    if (status.state == host::SessionState::Running)
        static_cast<void>(request(session.pause(true)));
    if (!dialogs.open(window.get(), kind, tag, message))
        return;
    update_focus();
}

void Application::file_selected(FileSelection selection) {
    if (!selection.error.empty()) {
        message = std::move(selection.error);
    } else if (!selection.canceled) {
        switch (selection.kind) {
        case FileKind::Cartridge:
            preferences.hardware.cartridge = std::move(selection.path);
            break;
        case FileKind::Firmware:
            preferences.hardware.pif = std::move(selection.path);
            break;
        case FileKind::TransferCartridge:
            if (selection.tag < preferences.hardware.ports.size()) {
                auto& port = preferences.hardware.ports[selection.tag];
                port.transfer.cartridge = std::move(selection.path);
                port.controller.connected = true;
                port.controller.device = ControllerDevice::Gamepad;
                port.controller.accessory = ControllerAccessory::TransferPak;
                port.controller_selected = port.accessory_selected = true;
            }
            break;
        case FileKind::ControllerPak:
            if (selection.tag < preferences.hardware.ports.size()) {
                auto& port = preferences.hardware.ports[selection.tag];
                port.pak_file = std::move(selection.path);
                port.controller.connected = true;
                port.controller.device = ControllerDevice::Gamepad;
                port.controller.accessory = ControllerAccessory::ControllerPak;
                port.controller_selected = port.accessory_selected = true;
            }
            break;
        }
        page = Page::Hardware;
        message = "Selected " + host::path_text(selection.kind == FileKind::Firmware
                                                    ? preferences.hardware.pif
                                                    : preferences.hardware.cartridge);
    }
    update_focus();
}

void Application::load_game(bool paused) {
    if (dialogs.pending() || quitting || (load_request != 0 && status.completed_request < load_request))
        return;
    if (preferences.hardware.cartridge.empty() || preferences.hardware.pif.empty() ||
        !preferences.hardware.save) {
        message = "Choose a cartridge, PIF firmware, and cartridge save hardware before loading.";
        page = Page::Hardware;
        return;
    }
    for (unsigned port = 0; port < preferences.hardware.ports.size(); ++port)
        mapper.set_configured_state(port, preferences.hardware.ports[port].controller);
    load_request = session.load(preferences.hardware, paused, storage_root);
    if (!request(load_request))
        return;
    load_started = SDL_GetTicks();
    run_started.reset();
    displayed_frames = last_frame_hash = 0;
    message.clear();
    page = Page::Game;
    update_focus();
}

void Application::pause_game() {
    if (!status.has_machine)
        return;
    if (request(session.pause(status.state == host::SessionState::Running))) {
        page = Page::Game;
        update_focus();
    }
}

void Application::request_quit(bool discard) {
    if (quitting)
        return;
    if (dialogs.pending()) {
        message = "Finish or cancel the file chooser before closing.";
        return;
    }
    quit_request = session.stop(discard);
    if (!request(quit_request))
        return;
    quitting = true;
    quit_discard = discard;
    quit_failed = false;
    update_focus();
}

void Application::save_settings() {
    preferences.input_bindings = host::encode_input_bindings(mapper.bindings());
    std::string error;
    if (!host::save_preferences(preferences_path, preferences, error))
        message = "Settings were not saved: " + error;
    else
        message = "Settings saved.";
}

void Application::update_volume() {
    std::string error;
    if (!audio.set_volume(preferences.volume, preferences.muted, error))
        message = "Audio: " + error;
}

void Application::reopen_audio() {
    std::string error;
    if (!audio.open(error)) {
        message = "Audio unavailable: " + error;
        return;
    }
    audio_running = false;
    update_volume();
}

void Application::set_fullscreen() {
    if (SDL_SetWindowFullscreen(window.get(), !fullscreen))
        fullscreen = !fullscreen;
    else
        message = SDL_GetError();
}

void Application::capture_frame() {
    auto path =
        timed_expired && !launch.capture.empty()
            ? launch.capture
            : preferences_path.parent_path() / ("capture-" + std::to_string(SDL_GetTicksNS()) + ".bmp");
    std::error_code code;
    if (std::filesystem::exists(path, code) || code) {
        message = "Capture destination already exists or cannot be inspected: " + host::path_text(path);
        std::cerr << message << '\n';
        exit_code = 1;
        return;
    }
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
    if (!surface || !SDL_SaveBMP(surface.get(), host::path_text(path).c_str())) {
        message = "Capture failed: " + std::string(SDL_GetError());
        std::cerr << message << '\n';
        exit_code = 1;
        return;
    }
    message = "Capture saved: " + host::path_text(path);
    std::cout << message << '\n';
}

} // namespace cupid::desktop
