#include "cupid/desktop/dialogs.hpp"

#include <SDL3/SDL_error.h>

#include <array>
#include <mutex>
#include <string_view>
#include <utility>

namespace cupid::desktop {
namespace {

using Filters = std::array<SDL_DialogFileFilter, 2>;

constexpr Filters CartridgeFilters{{{"Nintendo 64 cartridge", "z64;n64;v64"}, {"All files", "*"}}};
constexpr Filters FirmwareFilters{{{"PIF firmware", "rom;bin"}, {"All files", "*"}}};
constexpr Filters TransferCartridgeFilters{{{"Game Boy cartridge", "gb;gbc;sgb"}, {"All files", "*"}}};
constexpr Filters ControllerPakFilters{{{"Controller Pak image", "mpk;bin"}, {"All files", "*"}}};

const Filters& filters_for(FileKind kind) {
    switch (kind) {
    case FileKind::Cartridge:
        return CartridgeFilters;
    case FileKind::Firmware:
        return FirmwareFilters;
    case FileKind::TransferCartridge:
        return TransferCartridgeFilters;
    case FileKind::ControllerPak:
        return ControllerPakFilters;
    }
    return CartridgeFilters;
}

std::filesystem::path path_from_utf8(std::string_view text) {
    const std::u8string utf8(text.begin(), text.end());
    return std::filesystem::path(utf8);
}

} // namespace

struct FileDialogs::State {
    mutable std::mutex mutex;
    bool owner_alive{true};
    bool request_pending{};
    std::optional<FileSelection> result;
};

struct FileDialogs::Request {
    std::shared_ptr<FileDialogs::State> state;
    FileKind kind;
    unsigned tag;
};

void SDLCALL FileDialogs::dialog_complete(void* userdata, const char* const* filelist, int) noexcept {
    const std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    FileSelection selection{};
    selection.kind = request->kind;
    selection.tag = request->tag;

    try {
        if (filelist == nullptr) {
            const char* message = SDL_GetError();
            selection.error = message != nullptr ? message : "File dialog failed.";
            if (selection.error.empty())
                selection.error = "File dialog failed.";
        } else if (filelist[0] == nullptr) {
            selection.canceled = true;
        } else {
            const std::string utf8_path(filelist[0]);
            selection.path = path_from_utf8(utf8_path);
        }
    } catch (...) {
        selection.error = "The selected path could not be decoded as UTF-8.";
    }

    const auto state = request->state;
    std::scoped_lock lock(state->mutex);
    state->request_pending = false;
    if (state->owner_alive)
        state->result = std::move(selection);
}

FileDialogs::FileDialogs()
    : FileDialogs([](SDL_DialogFileCallback callback, void* userdata, SDL_Window*,
                     const SDL_DialogFileFilter* filters, int filter_count) {
          // SDL 3.4.16 retains its parent pointer for use by the native dialog thread.
          SDL_ShowOpenFileDialog(callback, userdata, nullptr, filters, filter_count, nullptr, false);
      }) {}

FileDialogs::FileDialogs(Launcher launcher)
    : state_(std::make_shared<State>()), launcher_(std::move(launcher)) {}

FileDialogs::~FileDialogs() {
    std::scoped_lock lock(state_->mutex);
    state_->owner_alive = false;
}

bool FileDialogs::open(SDL_Window* window, FileKind kind, unsigned tag, std::string& error) {
    error.clear();
    if (!launcher_) {
        error = "File dialog launcher is unavailable.";
        return false;
    }
    auto request = std::make_unique<Request>(Request{state_, kind, tag});

    {
        std::scoped_lock lock(state_->mutex);
        if (state_->request_pending) {
            error = "A file dialog is already open.";
            return false;
        }
        if (state_->result) {
            error = "The previous file selection is waiting to be processed.";
            return false;
        }
        state_->request_pending = true;
    }

    const auto& filters = filters_for(kind);
    launcher_(FileDialogs::dialog_complete, request.release(), window, filters.data(),
              static_cast<int>(filters.size()));
    return true;
}

bool FileDialogs::pending() const {
    std::scoped_lock lock(state_->mutex);
    return state_->request_pending;
}

std::optional<FileSelection> FileDialogs::take_result() {
    std::scoped_lock lock(state_->mutex);
    auto result = std::move(state_->result);
    state_->result.reset();
    return result;
}

} // namespace cupid::desktop
