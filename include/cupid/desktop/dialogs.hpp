#pragma once

#include <SDL3/SDL_dialog.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace cupid::desktop {

enum class FileKind { Cartridge, Firmware, TransferCartridge, ControllerPak };

struct FileSelection {
    FileKind kind{};
    unsigned tag{};
    std::filesystem::path path;
    bool canceled{};
    std::string error;
};

class FileDialogs {
  public:
    using Launcher = std::function<void(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window,
                                        const SDL_DialogFileFilter* filters, int filter_count)>;

    FileDialogs();
    explicit FileDialogs(Launcher launcher);
    ~FileDialogs();

    FileDialogs(const FileDialogs&) = delete;
    FileDialogs& operator=(const FileDialogs&) = delete;
    FileDialogs(FileDialogs&&) = delete;
    FileDialogs& operator=(FileDialogs&&) = delete;

    bool open(SDL_Window* window, FileKind kind, unsigned tag, std::string& error);
    [[nodiscard]] bool pending() const;
    [[nodiscard]] std::optional<FileSelection> take_result();

  private:
    struct State;
    struct Request;
    static void SDLCALL dialog_complete(void* userdata, const char* const* filelist, int filter) noexcept;
    std::shared_ptr<State> state_;
    Launcher launcher_;
};

} // namespace cupid::desktop
