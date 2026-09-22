#ifndef SDL_BUILDING_LIBRARY
#define SDL_BUILDING_LIBRARY
#endif
#include "cupid/desktop/dialogs.hpp"
#include "test.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {
using namespace cupid::desktop;

std::string sdl_error;
SDL_DialogFileCallback native_callback{};
void* native_userdata{};
SDL_Window* native_window{};
const SDL_DialogFileFilter* native_filters{};
int native_filter_count{};
const char* native_default_location{};
bool native_allow_many{};

struct PendingCallback {
    SDL_DialogFileCallback callback{};
    void* userdata{};
    SDL_Window* window{};
    const SDL_DialogFileFilter* filters{};
    int filter_count{};
};

FileDialogs::Launcher deferred(PendingCallback& pending) {
    return [&pending](SDL_DialogFileCallback callback, void* userdata, SDL_Window* window,
                      const SDL_DialogFileFilter* filters,
                      int filter_count) { pending = {callback, userdata, window, filters, filter_count}; };
}

std::string utf8_path(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace

extern "C" const char* SDLCALL SDL_GetError(void) {
    return sdl_error.c_str();
}

extern "C" void SDLCALL SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void* userdata,
                                               SDL_Window* window, const SDL_DialogFileFilter* filters,
                                               int nfilters, const char* default_location, bool allow_many) {
    native_callback = callback;
    native_userdata = userdata;
    native_window = window;
    native_filters = filters;
    native_filter_count = nfilters;
    native_default_location = default_location;
    native_allow_many = allow_many;
}

TEST(desktop_file_dialog_synchronous_success_copies_utf8_and_metadata) {
    const std::string selected = "C:/測試/é cartridge.z64";
    FileDialogs dialogs(
        [&](SDL_DialogFileCallback callback, void* userdata, SDL_Window*, const SDL_DialogFileFilter*, int) {
            const char* files[] = {selected.c_str(), nullptr};
            callback(userdata, files, 0);
        });

    std::string error;
    CHECK(dialogs.open(nullptr, FileKind::Cartridge, 41, error));
    CHECK(error.empty());
    CHECK(!dialogs.pending());

    const auto result = dialogs.take_result();
    CHECK(result.has_value());
    CHECK_EQ(result->kind, FileKind::Cartridge);
    CHECK_EQ(result->tag, 41U);
    CHECK_EQ(utf8_path(result->path), selected);
    CHECK(!result->canceled);
    CHECK(result->error.empty());
    CHECK(!dialogs.take_result().has_value());
}

TEST(desktop_file_dialog_serializes_pending_request_and_delivers_cancel) {
    PendingCallback pending;
    FileDialogs dialogs(deferred(pending));
    std::string error;

    CHECK(dialogs.open(nullptr, FileKind::Firmware, 7, error));
    CHECK(dialogs.pending());
    CHECK(!dialogs.open(nullptr, FileKind::Cartridge, 8, error));
    CHECK(error.find("already open") != std::string::npos);

    const char* canceled[] = {nullptr};
    pending.callback(pending.userdata, canceled, -1);
    CHECK(!dialogs.pending());
    const auto result = dialogs.take_result();
    CHECK(result.has_value());
    CHECK_EQ(result->kind, FileKind::Firmware);
    CHECK_EQ(result->tag, 7U);
    CHECK(result->canceled);
    CHECK(result->path.empty());
    CHECK(result->error.empty());
}

TEST(desktop_file_dialog_copies_sdl_error_before_callback_returns) {
    sdl_error = "native dialog 錯誤";
    FileDialogs dialogs([](SDL_DialogFileCallback callback, void* userdata, SDL_Window*,
                           const SDL_DialogFileFilter*, int) { callback(userdata, nullptr, -1); });

    std::string error;
    CHECK(dialogs.open(nullptr, FileKind::ControllerPak, 12, error));
    sdl_error = "changed after callback";

    const auto result = dialogs.take_result();
    CHECK(result.has_value());
    CHECK_EQ(result->kind, FileKind::ControllerPak);
    CHECK_EQ(result->tag, 12U);
    CHECK_EQ(result->error, std::string("native dialog 錯誤"));
    CHECK(!result->canceled);
}

TEST(desktop_file_dialog_callback_is_thread_safe) {
    PendingCallback pending;
    FileDialogs dialogs(deferred(pending));
    std::string error;
    CHECK(dialogs.open(nullptr, FileKind::TransferCartridge, 3, error));

    std::thread callback_thread([&] {
        const char* files[] = {"C:/遊戲/pocket.gb", nullptr};
        pending.callback(pending.userdata, files, 0);
    });
    while (dialogs.pending())
        std::this_thread::yield();
    callback_thread.join();

    const auto result = dialogs.take_result();
    CHECK(result.has_value());
    CHECK_EQ(result->kind, FileKind::TransferCartridge);
    CHECK_EQ(result->tag, 3U);
    CHECK_EQ(utf8_path(result->path), std::string("C:/遊戲/pocket.gb"));
}

TEST(desktop_file_dialog_callback_survives_owner_and_window_destruction) {
    PendingCallback pending;
    {
        int window_token = 0;
        auto* window = reinterpret_cast<SDL_Window*>(&window_token);
        FileDialogs dialogs(deferred(pending));
        std::string error;
        CHECK(dialogs.open(window, FileKind::Cartridge, 99, error));
        CHECK_EQ(pending.window, window);
        CHECK(dialogs.pending());
    }

    const char* files[] = {"C:/after-owner.z64", nullptr};
    pending.callback(pending.userdata, files, 0);
}

TEST(desktop_file_dialog_filters_remain_valid_until_async_completion) {
    struct Expected {
        FileKind kind;
        const char* label;
        const char* pattern;
    };
    const Expected expected[] = {
        {FileKind::Cartridge, "Nintendo 64 cartridge", "z64;n64;v64"},
        {FileKind::Firmware, "PIF firmware", "rom;bin"},
        {FileKind::TransferCartridge, "Game Boy cartridge", "gb;gbc;sgb"},
        {FileKind::ControllerPak, "Controller Pak image", "mpk;bin"},
    };

    for (const auto& item : expected) {
        PendingCallback pending;
        FileDialogs dialogs(deferred(pending));
        std::string error;
        CHECK(dialogs.open(nullptr, item.kind, 0, error));
        CHECK_EQ(pending.filter_count, 2);
        CHECK_EQ(std::string(pending.filters[0].name), std::string(item.label));
        CHECK_EQ(std::string(pending.filters[0].pattern), std::string(item.pattern));
        CHECK_EQ(std::string(pending.filters[1].pattern), std::string("*"));
        const char* canceled[] = {nullptr};
        pending.callback(pending.userdata, canceled, -1);
    }
}

TEST(desktop_file_dialog_default_launcher_calls_sdl_open_dialog) {
    native_callback = nullptr;
    native_userdata = nullptr;
    native_window = nullptr;
    native_filters = nullptr;
    native_filter_count = 0;
    native_default_location = reinterpret_cast<const char*>(1);
    native_allow_many = true;

    int window_token = 0;
    auto* window = reinterpret_cast<SDL_Window*>(&window_token);
    FileDialogs dialogs;
    std::string error;
    CHECK(dialogs.open(window, FileKind::Cartridge, 5, error));
    CHECK(native_callback != nullptr);
    CHECK(native_userdata != nullptr);
    CHECK(native_window == nullptr);
    CHECK_EQ(native_filter_count, 2);
    CHECK(native_filters != nullptr);
    CHECK(native_default_location == nullptr);
    CHECK(!native_allow_many);

    const char* canceled[] = {nullptr};
    native_callback(native_userdata, canceled, -1);
    CHECK(!dialogs.pending());
}

TEST(desktop_file_dialog_native_launch_does_not_retain_the_owner_window) {
    int owner = 0;
    auto* window = reinterpret_cast<SDL_Window*>(&owner);
    FileDialogs dialogs;
    std::string error;
    CHECK(dialogs.open(window, FileKind::Firmware, 6, error));
    const bool retains_window = native_window != nullptr;
    const char* canceled[] = {nullptr};
    native_callback(native_userdata, canceled, -1);
    CHECK(!retains_window);
}

TEST(desktop_file_dialog_preserves_a_result_until_it_is_consumed) {
    PendingCallback pending;
    FileDialogs dialogs(deferred(pending));
    std::string error;
    CHECK(dialogs.open(nullptr, FileKind::Firmware, 7, error));
    const char* selected[] = {"firmware.rom", nullptr};
    pending.callback(pending.userdata, selected, 0);
    const bool reopened = dialogs.open(nullptr, FileKind::Cartridge, 8, error);
    if (reopened) {
        const char* canceled[] = {nullptr};
        pending.callback(pending.userdata, canceled, -1);
    }
    CHECK(!reopened);
    const auto first = dialogs.take_result();
    CHECK(first.has_value());
    CHECK_EQ(first->kind, FileKind::Firmware);
    CHECK_EQ(first->tag, 7U);
    CHECK_EQ(utf8_path(first->path), "firmware.rom");
    CHECK(dialogs.open(nullptr, FileKind::Cartridge, 8, error));
    const char* canceled[] = {nullptr};
    pending.callback(pending.userdata, canceled, -1);
}
