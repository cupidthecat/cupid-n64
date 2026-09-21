#include "host_fixture.hpp"

#include "cupid/host/options.hpp"

using namespace cupid;
using namespace cupid::host;

TEST(host_pak_bank_options_accept_all_ports_and_capacities_in_either_argument_order) {
    std::string error;
    for (unsigned port = 1; port <= 4; ++port)
        for (unsigned banks = 1; banks <= 62; ++banks) {
            const auto capacity = std::to_string(port) + ':' + std::to_string(banks);
            const auto controller = std::to_string(port) + ":gamepad";
            const auto accessory = std::to_string(port) + ":controller-pak";
            for (bool capacity_first : {false, true}) {
                const auto options =
                    capacity_first
                        ? test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--pak-banks",
                                             capacity, "--controller", controller, "--accessory", accessory},
                                            error)
                        : test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller",
                                             controller, "--accessory", accessory, "--pak-banks", capacity},
                                            error);
                CHECK(options.has_value());
                for (unsigned index = 0; index < 4; ++index) {
                    CHECK_EQ(options->ports[index].pak_banks, index + 1 == port ? banks : 1U);
                    CHECK_EQ(options->ports[index].pak_banks_selected, index + 1 == port);
                }
            }
        }
}

TEST(host_pak_bank_options_reject_invalid_capacity_ports_and_repeated_settings) {
    std::string error;
    for (std::string_view value :
         {"1:0", "1:63", "1:-1", "1:4294967296", "1:2x", "1:", "0:2", "5:2", ":2", "2", "1:2:3"}) {
        CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller",
                                  "1:gamepad", "--accessory", "1:controller-pak", "--pak-banks", value},
                                 error));
        CHECK(error.find("--pak-banks") != std::string::npos);
    }
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--pak-banks"}, error));
    CHECK(error.find("Missing value") != std::string::npos);
    CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller", "1:gamepad",
                              "--accessory", "1:controller-pak", "--pak-banks", "1:2", "--pak-banks", "1:3"},
                             error));
    CHECK(error.find("Repeated option") != std::string::npos);
}

TEST(host_pak_bank_options_require_a_controller_pak_and_keep_default_capacity) {
    std::string error;
    for (std::string_view banks : {"1:1", "1:2"}) {
        CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--pak-banks", banks},
                                 error));
        CHECK(error.find("--pak-banks requires a Controller Pak") != std::string::npos);
        for (std::string_view accessory : {"1:none", "1:rumble-pak", "1:bio-sensor", "1:transfer-pak"}) {
            CHECK(!test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller",
                                      "1:gamepad", "--accessory", accessory, "--pak-banks", banks},
                                     error));
            CHECK(error.find("--pak-banks requires a Controller Pak") != std::string::npos);
        }
    }
    const auto options = test::host::parse({"game.z64", "--pif", "pif.rom", "--save", "none", "--controller",
                                            "1:gamepad", "--accessory", "1:controller-pak"},
                                           error);
    CHECK(options.has_value());
    CHECK_EQ(options->ports[0].pak_banks, 1U);
    CHECK(!options->ports[0].pak_banks_selected);
    CHECK(usage().find("--pak-banks PORT:COUNT") != std::string_view::npos);
}
