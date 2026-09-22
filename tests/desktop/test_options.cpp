#include "cupid/desktop/options.hpp"
#include "test.hpp"

#include <array>
#include <vector>

using namespace cupid;
using namespace cupid::desktop;

TEST(desktop_options_open_setup_without_images_and_preserve_unicode_paths) {
    std::string error;
    auto empty = parse_launch_options({}, error);
    CHECK(empty.has_value());
    CHECK(!empty->hardware);
    const std::array<std::string_view, 4> arguments{"--preferences", "settings space/Ω.conf", "--paused",
                                                    "--fullscreen"};
    auto parsed = parse_launch_options(arguments, error);
    CHECK(parsed.has_value());
    CHECK_EQ(host::path_text(parsed->preferences), "settings space/Ω.conf");
    CHECK(parsed->paused);
    CHECK(parsed->fullscreen);
}

TEST(desktop_options_reject_invalid_run_intervals_and_incomplete_capture_requests) {
    std::string error;
    for (const auto number : {"nan", "inf", "-1", "0", "0.001", "1x", "999999999999999999999"}) {
        const std::array<std::string_view, 2> arguments{"--run-for", number};
        CHECK(!parse_launch_options(arguments, error));
        CHECK(!error.empty());
    }
    const std::array<std::string_view, 2> capture{"--capture", "window.bmp"};
    CHECK(!parse_launch_options(capture, error));
    const std::array<std::string_view, 1> missing{"--preferences"};
    CHECK(!parse_launch_options(missing, error));
}

TEST(desktop_options_enable_the_first_controller_unless_it_was_explicitly_disabled) {
    std::string error;
    std::vector<std::string_view> arguments{"game.z64", "--pif",     "pif.rom", "--save",
                                            "eeprom4k", "--run-for", "0.5"};
    auto parsed = parse_launch_options(arguments, error);
    CHECK(parsed.has_value());
    CHECK(parsed->hardware->ports[0].controller.connected);
    CHECK_EQ(parsed->hardware->ports[0].controller.accessory, ControllerAccessory::ControllerPak);
    CHECK_EQ(parsed->run_seconds, 0.5);
    arguments.insert(arguments.end(), {"--controller", "1:none"});
    parsed = parse_launch_options(arguments, error);
    CHECK(parsed.has_value());
    CHECK(!parsed->hardware->ports[0].controller.connected);
}

TEST(desktop_options_preserve_hardware_values_that_resemble_desktop_flags) {
    std::string error;
    const std::array<std::string_view, 5> arguments{"--pif", "--renderer", "--save", "none", "game.z64"};
    const auto parsed = parse_launch_options(arguments, error);
    CHECK(parsed.has_value());
    CHECK_EQ(host::path_text(parsed->hardware->pif), "--renderer");
    CHECK(parsed->renderer.empty());
}

TEST(desktop_options_respect_positional_separator_when_adding_default_controller) {
    std::string error;
    const std::array<std::string_view, 6> arguments{"--pif", "pif.rom", "--save",
                                                    "none",  "--",      "--fullscreen"};
    const auto parsed = parse_launch_options(arguments, error);
    CHECK(parsed.has_value());
    CHECK_EQ(host::path_text(parsed->hardware->cartridge), "--fullscreen");
    CHECK(parsed->hardware->ports[0].controller.connected);
    CHECK(!parsed->fullscreen);
}

TEST(desktop_options_reject_a_headless_instruction_limit_instead_of_ignoring_it) {
    std::string error;
    const std::array<std::string_view, 7> arguments{
        "game.z64", "--pif", "pif.rom", "--save", "none", "--max-instructions", "1"};
    CHECK(!parse_launch_options(arguments, error));
    CHECK(error.find("--run-for") != std::string::npos);
}
