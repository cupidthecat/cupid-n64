#include "application_internal.hpp"

#include "cupid/desktop/application.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace cupid::desktop {

void sdl_check(bool success) {
    if (!success)
        throw std::runtime_error(SDL_GetError());
}

std::string fit_text(std::string_view value, std::size_t columns) {
    std::string result;
    result.reserve(std::min(columns, value.size()));
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte & 0xc0U) == 0x80U)
            continue;
        if (result.size() == columns)
            break;
        result.push_back(byte >= 32 && byte < 127 ? static_cast<char>(byte) : '?');
    }
    if (value.size() > columns && result.size() >= 3)
        result.replace(result.size() - 3, 3, "...");
    return result;
}

std::string_view state_name(host::SessionState state) {
    switch (state) {
    case host::SessionState::Empty:
        return "No cartridge";
    case host::SessionState::Loading:
        return "Loading";
    case host::SessionState::Running:
        return "Running";
    case host::SessionState::Paused:
        return "Paused";
    case host::SessionState::Faulted:
        return "Stopped after an error";
    }
    return "Unknown state";
}

Application::Application(LaunchOptions selected) : launch(std::move(selected)) {}

void Application::initialize() {
    preferences.hardware.ports[0].controller.connected = true;
    preferences.hardware.ports[0].controller.accessory = ControllerAccessory::ControllerPak;
    preferences.hardware.ports[0].controller_selected = true;
    preferences.hardware.ports[0].accessory_selected = true;
    preferences_path = launch.preferences;
    if (preferences_path.empty()) {
        std::unique_ptr<char, decltype(&SDL_free)> path(SDL_GetPrefPath("Cupid", "Cupid-N64"), SDL_free);
        if (!path)
            throw std::runtime_error(SDL_GetError());
        preferences_path = host::argument_path(path.get()) / "preferences.conf";
    }
    auto settings_directory = preferences_path.parent_path();
    if (settings_directory.empty())
        settings_directory = ".";
    storage_root = settings_directory / "saves";
    std::string error;
    if (!host::load_preferences(preferences_path, preferences, error))
        message = error;
    if (launch.hardware)
        preferences.hardware = *launch.hardware;
    auto bindings = host::default_input_bindings();
    if (!preferences.input_bindings.empty() &&
        !host::decode_input_bindings(preferences.input_bindings, bindings, error))
        message = "Saved controls could not be loaded: " + error;
    if (!mapper.set_bindings(bindings, error))
        throw std::runtime_error(error);
    for (unsigned port = 0; port < preferences.hardware.ports.size(); ++port)
        mapper.set_configured_state(port, preferences.hardware.ports[port].controller);

    window.reset(
        SDL_CreateWindow("Cupid-N64", 1120, 840, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    if (!window)
        throw std::runtime_error(SDL_GetError());
    sdl_check(SDL_SetWindowMinimumSize(window.get(), 960, 720));
    renderer.reset(
        SDL_CreateRenderer(window.get(), launch.renderer.empty() ? nullptr : launch.renderer.c_str()));
    if (!renderer)
        throw std::runtime_error(SDL_GetError());
    static_cast<void>(SDL_SetRenderVSync(renderer.get(), 1));
    video = std::make_unique<VideoOutput>(renderer.get());
    if (!input.open(error))
        message = "Gamepad input: " + error;
    reopen_audio();
    if (launch.fullscreen)
        set_fullscreen();
    if (launch.hardware)
        load_game(launch.paused);
    update_focus();
}

int Application::run() {
    initialize();
    while (!finished) {
        const Uint64 frame_start = SDL_GetTicks();
        SDL_Event next;
        while (SDL_PollEvent(&next))
            event(next);
        update();
        draw();
        const Uint64 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 8)
            SDL_Delay(static_cast<Uint32>(8 - elapsed));
    }
    input.set_rumble({});
    audio.close();
    return exit_code;
}

int run_application(LaunchOptions options) {
    sdl_check(SDL_Init(SDL_INIT_VIDEO));
    struct SdlLifetime {
        ~SdlLifetime() {
            SDL_Quit();
        }
    } lifetime;
    Application application(std::move(options));
    return application.run();
}

} // namespace cupid::desktop
