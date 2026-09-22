#include "application_internal.hpp"
#include "host/host_fixture.hpp"
#include "test.hpp"

#include <chrono>
#include <fstream>
#include <thread>

using namespace cupid;
using namespace cupid::desktop;
using namespace std::chrono_literals;

namespace {

struct VideoSystem {
    VideoSystem() {
        CHECK(SDL_Init(SDL_INIT_VIDEO));
    }
    ~VideoSystem() {
        SDL_Quit();
    }
};

LaunchOptions startup(test::host::TempDirectory& directory) {
    LaunchOptions options;
    options.preferences = directory.path() / "desktop.conf";
    options.renderer = "software";
    return options;
}

host::Options cartridge(test::host::TempDirectory& directory) {
    auto hardware = test::host::base_options(directory);
    std::vector<u8> firmware(1984);
    write_be32(firmware.data(), 0x1000ffff);
    hardware.pif = directory.write("loop-pif.rom", firmware);
    hardware.ports[0].controller.connected = true;
    hardware.ports[0].controller.accessory = ControllerAccessory::ControllerPak;
    return hardware;
}

void settle(Application& application, u64 request) {
    CHECK(request != 0);
    CHECK(application.session.wait(request, 5s));
    application.update();
}

} // namespace

TEST(desktop_application_setup_draws_all_pages_and_exits_cleanly) {
    test::host::TempDirectory directory;
    VideoSystem video;
    Application application(startup(directory));
    application.initialize();
    CHECK_EQ(application.page, Page::Hardware);
    CHECK(!application.status.has_machine);
    application.update();
    application.draw();
    CHECK(!application.buttons.empty());
    application.set_page(Page::Controls);
    application.draw();
    application.set_page(Page::Game);
    application.draw();
    application.request_quit();
    settle(application, application.quit_request);
    CHECK(application.finished);
    CHECK_EQ(application.exit_code, 0);
    CHECK(std::filesystem::exists(application.preferences_path));
}

TEST(desktop_application_keyboard_remapping_persists_without_leaking_capture_input) {
    test::host::TempDirectory directory;
    VideoSystem video;
    const auto launch = startup(directory);
    {
        Application application(launch);
        application.initialize();
        application.set_page(Page::Controls);
        application.begin_capture(static_cast<unsigned>(host::N64Button::A), true);
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.scancode = SDL_SCANCODE_C;
        key.key.down = true;
        application.event(key);
        CHECK(!application.binding_capture);
        CHECK_EQ(application.mapper.bindings().ports[0].buttons[0].keyboard_scancode, SDL_SCANCODE_C);
        CHECK_EQ(application.mapper.controller_states()[0].buttons, 0U);
    }
    Application restored(launch);
    restored.initialize();
    CHECK_EQ(restored.mapper.bindings().ports[0].buttons[0].keyboard_scancode, SDL_SCANCODE_C);
}

TEST(desktop_application_load_pause_focus_loss_and_stop_use_the_real_session) {
    test::host::TempDirectory directory;
    VideoSystem video;
    Application application(startup(directory));
    application.initialize();
    application.preferences.hardware = cartridge(directory);
    application.load_game(true);
    settle(application, application.load_request);
    CHECK(application.status.has_machine);
    CHECK_EQ(application.status.state, host::SessionState::Paused);
    application.pause_game();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (application.session.status().state != host::SessionState::Running &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    application.update();
    CHECK_EQ(application.status.state, host::SessionState::Running);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_X;
    key.key.down = true;
    application.event(key);
    CHECK_EQ(application.mapper.controller_states()[0].buttons, 0x8000U);
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    application.event(focus);
    CHECK_EQ(application.mapper.controller_states()[0].buttons, 0U);
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    application.event(focus);
    CHECK_EQ(application.mapper.controller_states()[0].buttons, 0U);
    settle(application, application.session.pause(true));
    CHECK_EQ(application.status.state, host::SessionState::Paused);
    application.request_quit();
    settle(application, application.quit_request);
    CHECK(application.finished);
    CHECK(!application.status.has_machine);
}

TEST(desktop_application_failed_save_retains_the_session_and_allows_retry) {
    test::host::TempDirectory directory;
    VideoSystem video;
    Application application(startup(directory));
    application.initialize();
    application.preferences.hardware = cartridge(directory);
    application.preferences.hardware.save = SaveType::Sram;
    application.preferences.hardware.save_file = directory.path() / "absent" / "cart.sra";
    application.load_game(true);
    settle(application, application.load_request);
    CHECK(application.status.has_machine);
    application.request_quit();
    settle(application, application.quit_request);
    CHECK(!application.finished);
    CHECK(application.quit_failed);
    CHECK(application.status.has_machine);
    CHECK(!application.status.error.empty());
    CHECK(std::filesystem::create_directory(directory.path() / "absent"));
    application.request_quit();
    settle(application, application.quit_request);
    CHECK(application.finished);
    CHECK_EQ(std::filesystem::file_size(directory.path() / "absent" / "cart.sra"), 32768U);
}

TEST(desktop_application_timed_paused_run_captures_the_rendered_window) {
    test::host::TempDirectory directory;
    VideoSystem video;
    auto launch = startup(directory);
    launch.hardware = cartridge(directory);
    launch.run_seconds = 0.05;
    launch.paused = true;
    launch.capture = directory.path() / "window.bmp";
    Application application(launch);
    CHECK_EQ(application.run(), 0);
    std::ifstream captured(launch.capture, std::ios::binary);
    CHECK(captured.good());
    CHECK_EQ(captured.get(), 'B');
    CHECK_EQ(captured.get(), 'M');
    CHECK(std::filesystem::file_size(launch.capture) > 1024U);
    CHECK(application.finished);
    CHECK_EQ(application.last_run_status.instructions, 0U);
}

TEST(desktop_application_timed_run_treats_zero_tick_as_a_valid_start_time) {
    test::host::TempDirectory directory;
    VideoSystem video;
    Application application(startup(directory));
    application.initialize();
    application.launch.run_seconds = 0.0001;
    application.run_started = Uint64{0};
    SDL_Delay(2);

    application.update();

    CHECK(application.timed_expired);
}
