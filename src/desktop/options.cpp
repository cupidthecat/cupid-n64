#include "cupid/desktop/options.hpp"

#include <charconv>
#include <cmath>
#include <vector>

namespace cupid::desktop {

std::optional<LaunchOptions> parse_launch_options(std::span<const std::string_view> arguments,
                                                  std::string& error) {
    error.clear();
    LaunchOptions options;
    std::vector<std::string_view> hardware_arguments;
    std::optional<std::string_view> first_controller;
    bool first_accessory = false;
    bool first_transfer = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--") {
            hardware_arguments.insert(hardware_arguments.end(),
                                      arguments.begin() + static_cast<std::ptrdiff_t>(index),
                                      arguments.end());
            break;
        }
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return options;
        }
        if (argument == "--paused") {
            options.paused = true;
            continue;
        }
        if (argument == "--fullscreen") {
            options.fullscreen = true;
            continue;
        }
        if (argument == "--preferences" || argument == "--capture" || argument == "--renderer" ||
            argument == "--run-for") {
            if (++index == arguments.size() || arguments[index].empty()) {
                error = "Missing value for " + std::string(argument) + '.';
                return std::nullopt;
            }
            const auto value = arguments[index];
            if (argument == "--preferences")
                options.preferences = host::argument_path(value);
            else if (argument == "--capture")
                options.capture = host::argument_path(value);
            else if (argument == "--renderer")
                options.renderer = value;
            else {
                const auto [end, code] =
                    std::from_chars(value.data(), value.data() + value.size(), options.run_seconds);
                if (code != std::errc{} || end != value.data() + value.size() ||
                    !std::isfinite(options.run_seconds) || options.run_seconds < 0.05 ||
                    options.run_seconds > 86400) {
                    error = "--run-for must be a finite number of seconds between 0.05 and 86400.";
                    return std::nullopt;
                }
            }
        } else {
            if (argument == "--max-instructions") {
                error = "Use --run-for to bound a desktop session; --max-instructions belongs to the "
                        "headless runner.";
                return std::nullopt;
            }
            if (argument == "--require-test-success" || argument == "--require-extended-tests") {
                error = "Test-report assertions are supported by the cupid-n64 command-line runner.";
                return std::nullopt;
            }
            hardware_arguments.push_back(argument);
            if (argument.starts_with('-') && argument != "--rtc" && index + 1 < arguments.size()) {
                const auto value = arguments[++index];
                hardware_arguments.push_back(value);
                if (value.starts_with("1:")) {
                    if (argument == "--controller")
                        first_controller = value.substr(2);
                    else if (argument == "--accessory")
                        first_accessory = true;
                    else if (argument == "--transfer-rom")
                        first_transfer = true;
                }
            }
        }
    }
    if (!hardware_arguments.empty()) {
        if (!first_controller)
            hardware_arguments.insert(hardware_arguments.begin(), {"--controller", "1:gamepad"});
        if ((!first_controller || *first_controller == "gamepad") && !first_accessory && !first_transfer)
            hardware_arguments.insert(hardware_arguments.begin(), {"--accessory", "1:controller-pak"});
        options.hardware = host::parse_options(hardware_arguments, error);
        if (!options.hardware)
            return std::nullopt;
        if (options.hardware->require_success || options.hardware->require_extended) {
            error = "Test-report assertions are supported by the cupid-n64 command-line runner.";
            return std::nullopt;
        }
    }
    if (options.run_seconds != 0 && !options.hardware) {
        error = "--run-for requires cartridge and hardware arguments.";
        return std::nullopt;
    }
    if (!options.capture.empty() && options.run_seconds == 0) {
        error = "--capture requires --run-for; interactive captures use F12.";
        return std::nullopt;
    }
    return options;
}

std::string_view usage() {
    return "Usage: cupid-desktop [cartridge --pif firmware --save type] [hardware options]\n"
           "With no cartridge arguments, the window opens its hardware setup page.\n"
           "\nDesktop options:\n"
           "  --preferences file  Read and atomically save settings at this path\n"
           "  --paused            Load without starting emulation\n"
           "  --fullscreen        Start in fullscreen mode\n"
           "  --renderer name     Select an SDL renderer, such as software\n"
           "  --run-for seconds   Run for a measured wall-clock interval, then save and exit\n"
           "  --capture file.bmp  Capture the final window before --run-for exits\n"
           "  --help              Show this help and hardware options\n"
           "\nKeys: Escape pause/resume, F5 reset, F11 fullscreen, F12 capture.\n";
}

} // namespace cupid::desktop
