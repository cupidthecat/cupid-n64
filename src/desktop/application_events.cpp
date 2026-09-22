#include "application_internal.hpp"

#include <algorithm>
#include <cctype>

namespace cupid::desktop {

void Application::activate(float x, float y) {
    for (const auto& current : buttons) {
        const auto& area = current.rectangle;
        if (current.enabled && x >= area.x && x < area.x + area.w && y >= area.y && y < area.y + area.h) {
            auto action = current.action;
            action();
            return;
        }
    }
}

void Application::event(const SDL_Event& next) {
    input.event(next);
    if (capture_event(next))
        return;
    switch (next.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        request_quit();
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused = false;
        update_focus();
        session.set_input(mapper.controller_states());
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused = true;
        update_focus();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        sdl_check(SDL_RenderCoordinatesFromWindow(renderer.get(), next.motion.x, next.motion.y, &mouse_x,
                                                  &mouse_y));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (next.button.button == SDL_BUTTON_LEFT) {
            float x = 0;
            float y = 0;
            sdl_check(SDL_RenderCoordinatesFromWindow(renderer.get(), next.button.x, next.button.y, &x, &y));
            activate(x, y);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (next.key.repeat || quitting || quit_failed)
            break;
        switch (next.key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            pause_game();
            break;
        case SDL_SCANCODE_F5:
            static_cast<void>(request(session.reset()));
            break;
        case SDL_SCANCODE_F11:
            set_fullscreen();
            break;
        case SDL_SCANCODE_F12:
            screenshot_requested = true;
            break;
        default:
            break;
        }
        break;
    case SDL_EVENT_DROP_FILE:
        if (next.drop.data && !dialogs.pending() && !quitting) {
            const auto path = host::argument_path(next.drop.data);
            auto extension = host::path_text(path.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
            FileKind kind = FileKind::Cartridge;
            std::error_code code;
            const auto size = std::filesystem::file_size(path, code);
            if (!code && (size == 1984 || size == 2048) && (extension == ".bin" || extension == ".rom"))
                kind = FileKind::Firmware;
            else if (extension == ".gb" || extension == ".gbc")
                kind = FileKind::TransferCartridge;
            else if (extension == ".mpk" || extension == ".pak")
                kind = FileKind::ControllerPak;
            if (status.state == host::SessionState::Running)
                static_cast<void>(request(session.pause(true)));
            file_selected({kind, input_port, path, false, {}});
        }
        break;
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        if (!next.adevice.recording) {
            const auto state = audio.status();
            if (!state.available) {
                audio.close();
                audio_running = false;
                message = "Audio device disconnected. Use Retry audio after reconnecting it.";
            }
        }
        break;
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_TARGETS_RESET:
        video->clear();
        message = "Video device reset. The next emulated field will restore the picture.";
        break;
    default:
        break;
    }
}

void Application::begin_capture(unsigned selected, bool keyboard) {
    if (selected >= static_cast<unsigned>(host::N64Button::Count) + 6U)
        return;
    binding_capture = BindingCapture{input_port, selected, keyboard};
    message.clear();
    update_focus();
}

void Application::change_bindings(host::InputBindings updated) {
    std::string error;
    if (!mapper.set_bindings(updated, error)) {
        message = "Controls were not changed: " + error;
        return;
    }
    save_settings();
}

bool Application::capture_event(const SDL_Event& next) {
    if (!binding_capture)
        return false;
    const auto capture = *binding_capture;
    if (next.type == SDL_EVENT_KEY_DOWN && next.key.scancode == SDL_SCANCODE_ESCAPE) {
        binding_capture.reset();
        update_focus();
        return true;
    }
    auto updated = mapper.bindings();
    auto& port = updated.ports[capture.port];
    constexpr unsigned digital_count = static_cast<unsigned>(host::N64Button::Count);
    if (capture.keyboard) {
        if (next.type != SDL_EVENT_KEY_DOWN || next.key.repeat)
            return next.type == SDL_EVENT_KEY_UP;
        const int key = next.key.scancode == SDL_SCANCODE_BACKSPACE ? host::KeyboardScancodeUnbound
                                                                    : static_cast<int>(next.key.scancode);
        if (capture.button < digital_count) {
            port.buttons[capture.button].keyboard_scancode = key;
        } else {
            std::array<int*, 4> stick_keys{&port.stick.left_key, &port.stick.right_key, &port.stick.up_key,
                                           &port.stick.down_key};
            const unsigned index = capture.button - digital_count;
            if (index >= stick_keys.size())
                return true;
            *stick_keys[index] = key;
        }
        port.keyboard_enabled = true;
    } else {
        const auto id = mapper.assigned_gamepad(capture.port);
        if (next.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && id == next.gbutton.which &&
            capture.button < digital_count) {
            const auto mapped = gamepad_button(next.gbutton.button);
            if (!mapped)
                return true;
            auto& binding = port.buttons[capture.button];
            binding.gamepad_button = *mapped;
            binding.gamepad_axis = host::GamepadAxis::Count;
        } else if (next.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && id == next.gaxis.which &&
                   (next.gaxis.value >= 16000 || next.gaxis.value <= -16000)) {
            const auto mapped = gamepad_axis(next.gaxis.axis);
            if (!mapped)
                return true;
            if (capture.button < digital_count) {
                auto& binding = port.buttons[capture.button];
                binding.gamepad_button = host::GamepadButton::Count;
                binding.gamepad_axis = *mapped;
                binding.axis_direction =
                    next.gaxis.value < 0 ? host::AxisDirection::Negative : host::AxisDirection::Positive;
            } else if (capture.button == digital_count + 4) {
                port.stick.x_axis = *mapped;
            } else if (capture.button == digital_count + 5) {
                port.stick.y_axis = *mapped;
            } else {
                return true;
            }
        } else {
            return next.type == SDL_EVENT_KEY_DOWN || next.type == SDL_EVENT_KEY_UP;
        }
        port.gamepad_enabled = true;
    }
    change_bindings(std::move(updated));
    binding_capture.reset();
    update_focus();
    return true;
}

} // namespace cupid::desktop
