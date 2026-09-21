#include "host_fixture.hpp"

#include "cupid/host/hardware.hpp"
#include "cupid/host/storage.hpp"

#include <algorithm>
#include <limits>

using namespace cupid;
using namespace cupid::host;

TEST(host_pak_banks_roundtrip_every_configured_bank_on_all_ports) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    constexpr std::array<unsigned, 4> counts{1, 2, 16, 62};
    for (unsigned port = 0; port < 4; ++port) {
        options.ports[port].controller.connected = true;
        options.ports[port].controller.accessory = ControllerAccessory::ControllerPak;
        options.ports[port].pak_banks = counts[port];
        options.ports[port].pak_file = directory.path() / ("port-" + std::to_string(port) + ".pak");
    }
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    for (unsigned port = 0; port < 4; ++port) {
        auto& bytes = system->bus.controller_paks[port];
        CHECK_EQ(bytes.size(), counts[port] * 32768U);
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<u8>((index / 32768U) * 17U + (index % 32768U) + port * 29U);
    }
    const auto expected = system->bus.controller_paks;
    CHECK(flush_persistent_storage(*system, options, error));
    system.reset();
    for (unsigned port = 0; port < 4; ++port)
        CHECK_EQ(std::filesystem::file_size(options.ports[port].pak_file), counts[port] * 32768U);
    system = create_system(options, error);
    CHECK(system != nullptr);
    CHECK_EQ(system->bus.controller_paks, expected);
}

TEST(host_pak_banks_reject_wrong_file_size_without_partial_load_or_overwrite) {
    test::host::TempDirectory directory;
    auto options = test::host::base_options(directory);
    options.ports[0].controller.connected = true;
    options.ports[0].controller.accessory = ControllerAccessory::ControllerPak;
    options.ports[0].pak_banks = 3;
    std::string error;
    auto system = create_system(options, error);
    CHECK(system != nullptr);
    std::fill(system->bus.controller_paks[0].begin(), system->bus.controller_paks[0].end(), u8{0xa5});
    const auto expected = system->bus.controller_paks;
    for (const std::size_t size : {32768U, 98303U, 98305U, 131072U}) {
        const std::vector<u8> original(size, 0x5a);
        options.ports[0].pak_file = directory.write("wrong-size.pak", original);
        CHECK(!load_persistent_storage(*system, options, error));
        CHECK(error.find("exactly 98304 bytes") != std::string::npos);
        CHECK_EQ(system->bus.controller_paks, expected);
        CHECK_EQ(directory.read(options.ports[0].pak_file), original);
        CHECK(!create_system(options, error));
        CHECK_EQ(directory.read(options.ports[0].pak_file), original);
    }
}

TEST(host_pak_banks_reject_invalid_capacity_on_each_port) {
    test::host::TempDirectory directory;
    std::string error;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned banks : {0U, 63U, std::numeric_limits<unsigned>::max()}) {
            auto options = test::host::base_options(directory);
            options.ports[port].controller.connected = true;
            options.ports[port].controller.accessory = ControllerAccessory::ControllerPak;
            options.ports[port].pak_banks = banks;
            CHECK(!create_system(options, error));
            CHECK(error.find("between 1 and 62 banks") != std::string::npos);
        }
}
