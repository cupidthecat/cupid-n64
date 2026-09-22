#include "host_fixture.hpp"

#include "cupid/host/hardware.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace cupid;
using namespace cupid::host;

std::optional<Options> parse(std::vector<std::string> arguments, std::string& error) {
    std::vector<std::string_view> views;
    views.reserve(arguments.size());
    for (const auto& argument : arguments)
        views.emplace_back(argument);
    return parse_options(views, error);
}

std::vector<std::string> base_arguments() {
    return {"game.z64", "--pif", "pif.rom", "--save", "none"};
}

std::vector<u8> command(Bus& bus, unsigned port, std::initializer_list<u8> input, u8 receive) {
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    unsigned offset = port;
    bus.pif[0x7c0 + offset++] = static_cast<u8>(input.size());
    bus.pif[0x7c0 + offset++] = receive;
    for (u8 value : input)
        bus.pif[0x7c0 + offset++] = value;
    const unsigned response = 0x7c0 + offset;
    std::fill_n(bus.pif.begin() + response, receive, u8{0xcc});
    bus.pif[response + receive] = 0xfe;
    bus.joybus.configure();
    bus.joybus.execute();
    return {bus.pif.begin() + response, bus.pif.begin() + response + receive};
}

} // namespace

TEST(host_gamecube_controller_option_selects_each_port_with_neutral_n64_defaults) {
    std::string error;
    for (unsigned port = 1; port <= 4; ++port) {
        auto arguments = base_arguments();
        arguments.insert(arguments.end(), {"--controller", std::to_string(port) + ":gamecube"});
        const auto options = parse(std::move(arguments), error);
        CHECK(options.has_value());
        for (unsigned index = 0; index < options->ports.size(); ++index) {
            const auto& selected = options->ports[index];
            CHECK_EQ(selected.controller.connected, index + 1 == port);
            CHECK_EQ(selected.controller.device,
                     index + 1 == port ? ControllerDevice::GameCube : ControllerDevice::Gamepad);
            CHECK_EQ(selected.controller.accessory, ControllerAccessory::None);
            CHECK_EQ(selected.controller.buttons, 0U);
            CHECK_EQ(selected.controller.stick_x, 0);
            CHECK_EQ(selected.controller.stick_y, 0);
            CHECK_EQ(selected.pak_banks, 1U);
            CHECK(!selected.pak_banks_selected);
            CHECK_EQ(selected.controller_selected, index + 1 == port);
        }
    }
    CHECK(usage().find("--controller PORT:gamepad|mouse|gamecube|none") != std::string_view::npos);
}

TEST(host_gamecube_controller_option_rejects_n64_accessories_and_pak_settings) {
    std::string error;
    for (std::string_view accessory : {"controller-pak", "rumble-pak", "bio-sensor", "transfer-pak"}) {
        auto arguments = base_arguments();
        arguments.insert(arguments.end(),
                         {"--controller", "1:gamecube", "--accessory", "1:" + std::string(accessory)});
        CHECK(!parse(std::move(arguments), error));
        CHECK(error.find("accessories require a connected gamepad") != std::string::npos);
    }

    for (const auto& [option, value, expected] :
         std::array{std::array<std::string_view, 3>{"--pak-file", "1:pak.bin",
                                                    "--pak-file requires a Controller Pak"},
                    std::array<std::string_view, 3>{"--pak-banks", "1:2",
                                                    "--pak-banks requires a Controller Pak"}}) {
        auto arguments = base_arguments();
        arguments.insert(arguments.end(),
                         {"--controller", "1:gamecube", std::string(option), std::string(value)});
        CHECK(!parse(std::move(arguments), error));
        CHECK(error.find(expected) != std::string::npos);
    }

    auto arguments = base_arguments();
    arguments.insert(arguments.end(), {"--controller", "1:gamecube", "--transfer-rom", "1:game.gb"});
    CHECK(!parse(std::move(arguments), error));
    CHECK(error.find("accessories require a connected gamepad") != std::string::npos);
}

TEST(host_gamecube_controller_option_creates_the_selected_device_with_neutral_protocol_state) {
    test::host::TempDirectory directory;
    const auto cartridge = directory.write("cartridge.z64", test::host::n64_rom());
    const auto pif = directory.write("pif.rom", test::host::pif());
    std::string error;

    for (unsigned port = 1; port <= 4; ++port) {
        auto options = parse({path_text(cartridge), "--pif", path_text(pif), "--pif-region", "ntsc", "--cic",
                              "6102", "--save", "none", "--controller", std::to_string(port) + ":gamecube"},
                             error);
        CHECK(options.has_value());
        auto system = create_system(*options, error);
        CHECK(system != nullptr);
        const unsigned index = port - 1;
        const auto& controller = system->bus.controllers()[index];
        CHECK(controller.connected);
        CHECK_EQ(controller.device, ControllerDevice::GameCube);
        CHECK_EQ(controller.accessory, ControllerAccessory::None);
        CHECK_EQ(command(system->bus, index, {0x40, 3, 0}, 8),
                 (std::vector<u8>{0x20, 0x80, 127, 127, 127, 127, 0, 0}));
    }
}
