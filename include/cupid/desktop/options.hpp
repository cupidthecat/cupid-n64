#pragma once

#include "cupid/host/options.hpp"

namespace cupid::desktop {

struct LaunchOptions {
    std::optional<host::Options> hardware;
    std::filesystem::path preferences;
    std::filesystem::path capture;
    std::string renderer;
    double run_seconds{};
    bool paused{};
    bool fullscreen{};
    bool help{};
};

[[nodiscard]] std::optional<LaunchOptions> parse_launch_options(std::span<const std::string_view> arguments,
                                                                std::string& error);
[[nodiscard]] std::string_view usage();

} // namespace cupid::desktop
