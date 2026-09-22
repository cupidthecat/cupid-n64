#pragma once

#include "cupid/desktop/audio.hpp"
#include "cupid/desktop/dialogs.hpp"
#include "cupid/desktop/input.hpp"
#include "cupid/desktop/options.hpp"
#include "cupid/desktop/video.hpp"
#include "cupid/host/preferences.hpp"
#include "cupid/host/session.hpp"

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cupid::desktop {

enum class Page { Game, Hardware, Controls };

struct Button {
    SDL_FRect rectangle;
    std::function<void()> action;
    bool enabled{true};
};

struct BindingCapture {
    unsigned port{};
    unsigned button{};
    bool keyboard{};
};

struct Application {
    LaunchOptions launch;
    host::Preferences preferences;
    std::filesystem::path preferences_path;
    std::filesystem::path storage_root;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    std::unique_ptr<VideoOutput> video;
    host::InputMapper mapper{std::array<ControllerState, 4>{}};
    InputDevices input{mapper};
    AudioOutput audio;
    FileDialogs dialogs;
    host::Session session;
    host::SessionStatus status;
    host::SessionStatus last_run_status;
    AudioStatus last_audio_status;
    Page page{Page::Hardware};
    std::vector<Button> buttons;
    std::optional<BindingCapture> binding_capture;
    unsigned input_port{};
    bool focused{true};
    bool fullscreen{};
    bool smoothing{true};
    bool audio_running{};
    bool quitting{};
    bool quit_discard{};
    bool quit_failed{};
    bool finished{};
    bool screenshot_requested{};
    bool timed_expired{};
    int exit_code{};
    u64 quit_request{};
    u64 load_request{};
    u64 output_epoch{};
    u64 displayed_frames{};
    u64 last_frame_hash{};
    Uint64 load_started{};
    std::optional<Uint64> run_started;
    float ui_width{560};
    float ui_height{420};
    float mouse_x{-1};
    float mouse_y{-1};
    std::string message;
    std::string last_session_error;
    std::string last_audio_error;
    std::string last_title;

    explicit Application(LaunchOptions selected);
    int run();
    void initialize();
    void event(const SDL_Event& event);
    void update();
    void draw();
    void draw_game();
    void draw_hardware();
    void draw_controls();
    void draw_toolbar();
    void draw_footer();
    void draw_quit_failure();
    void draw_binding_capture();
    void text(float x, float y, std::string_view value, bool dim = false);
    void button(float x, float y, float width, std::string label, std::function<void()> action,
                bool enabled = true);
    void activate(float x, float y);
    void set_page(Page selected);
    void update_focus();
    void open_file(FileKind kind, unsigned tag = 0);
    void file_selected(FileSelection selection);
    void load_game(bool paused = false);
    void pause_game();
    void request_quit(bool discard = false);
    void save_settings();
    void update_volume();
    void reopen_audio();
    void set_fullscreen();
    void capture_frame();
    void begin_capture(unsigned button, bool keyboard);
    bool capture_event(const SDL_Event& event);
    void change_bindings(host::InputBindings updated);
    bool request(u64 id);
    void report_run() const;
};

void sdl_check(bool success);
[[nodiscard]] std::string fit_text(std::string_view value, std::size_t columns);
[[nodiscard]] std::string_view state_name(host::SessionState state);

} // namespace cupid::desktop
