#pragma once

#include "cupid/host/options.hpp"

#include <filesystem>
#include <string>

namespace cupid::host {

struct Preferences {
    Options hardware;
    float volume{1.0F};
    bool muted{};
    std::string input_bindings;
};

[[nodiscard]] bool load_preferences(const std::filesystem::path& path, Preferences& preferences,
                                    std::string& error);
[[nodiscard]] bool save_preferences(const std::filesystem::path& path, const Preferences& preferences,
                                    std::string& error);
[[nodiscard]] bool prepare_storage_paths(const System& system, Options& options,
                                         const std::filesystem::path& storage_root, std::string& error);

} // namespace cupid::host
