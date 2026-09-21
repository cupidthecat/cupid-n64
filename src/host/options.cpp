#include "cupid/host/options.hpp"

#include <charconv>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace cupid::host {
namespace {

template <class T> struct Choice {
    std::string_view name;
    T value;
};

template <class T>
    requires(std::is_enum_v<T> || std::is_same_v<T, bool>)
bool select(std::string_view name, std::string_view value, T& result,
            std::initializer_list<Choice<T>> choices, std::string& error) {
    for (const auto& choice : choices) {
        if (choice.name == value) {
            result = choice.value;
            return true;
        }
    }
    error = "Invalid value for " + std::string(name) + ": " + std::string(value) + ". Expected ";
    bool first = true;
    for (const auto& choice : choices) {
        if (!first)
            error += ", ";
        error += choice.name;
        first = false;
    }
    error += '.';
    return false;
}

template <class T>
bool select(std::string_view name, std::string_view value, std::optional<T>& result,
            std::initializer_list<Choice<T>> choices, std::string& error) {
    T selected{};
    if (!select(name, value, selected, choices, error))
        return false;
    result = selected;
    return true;
}

template <class T> bool number(std::string_view name, std::string_view value, T& result, std::string& error) {
    const auto [end, status] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (status == std::errc{} && end == value.data() + value.size())
        return true;
    error = std::string(name) + " requires a decimal integer in range.";
    return false;
}

bool port_value(std::string_view name, std::string_view value, unsigned& port, std::string_view& setting,
                std::string& error) {
    const auto colon = value.find(':');
    if (colon == std::string_view::npos || colon + 1 == value.size() ||
        !number(name, value.substr(0, colon), port, error) || port < 1 || port > 4) {
        error = std::string(name) + " requires PORT:VALUE with a port from 1 to 4.";
        return false;
    }
    --port;
    setting = value.substr(colon + 1);
    return true;
}

bool is_port_option(std::string_view name) {
    return name == "--controller" || name == "--accessory" || name == "--pak-file" || name == "--pak-banks" ||
           name == "--transfer-rom" || name == "--transfer-save" || name == "--transfer-mapper" ||
           name == "--transfer-ram" || name == "--transfer-rtc" || name == "--transfer-rtc-file" ||
           name == "--transfer-rumble";
}

bool set_port(std::string_view name, std::string_view value, PortOptions& port, std::string& error) {
    if (name == "--controller") {
        if (value == "none") {
            port.controller.connected = false;
        } else {
            if (!select(name, value, port.controller.device,
                        {{"gamepad", ControllerDevice::Gamepad}, {"mouse", ControllerDevice::Mouse}}, error))
                return false;
            port.controller.connected = true;
        }
        port.controller_selected = true;
    } else if (name == "--accessory") {
        if (!select(name, value, port.controller.accessory,
                    {{"none", ControllerAccessory::None},
                     {"controller-pak", ControllerAccessory::ControllerPak},
                     {"rumble-pak", ControllerAccessory::RumblePak},
                     {"bio-sensor", ControllerAccessory::BioSensor},
                     {"transfer-pak", ControllerAccessory::TransferPak}},
                    error))
            return false;
        port.accessory_selected = true;
    } else if (name == "--pak-file") {
        port.pak_file = argument_path(value);
    } else if (name == "--pak-banks") {
        if (!number(name, value, port.pak_banks, error))
            return false;
        if (port.pak_banks < 1 || port.pak_banks > 62) {
            error = "--pak-banks must select between 1 and 62 banks.";
            return false;
        }
        port.pak_banks_selected = true;
    } else if (name == "--transfer-rom") {
        port.transfer.cartridge = argument_path(value);
    } else if (name == "--transfer-save") {
        port.transfer.save_file = argument_path(value);
    } else if (name == "--transfer-rtc-file") {
        port.transfer.rtc_file = argument_path(value);
    } else if (name == "--transfer-mapper") {
        return select(name, value, port.transfer.mapper,
                      {{"linear", GameBoyMapper::Linear},
                       {"mbc1", GameBoyMapper::Mbc1},
                       {"mbc2", GameBoyMapper::Mbc2},
                       {"mbc3", GameBoyMapper::Mbc3},
                       {"mbc30", GameBoyMapper::Mbc30},
                       {"mbc5", GameBoyMapper::Mbc5}},
                      error);
    } else if (name == "--transfer-ram") {
        unsigned bytes = 0;
        if (!number(name, value, bytes, error))
            return false;
        port.transfer.ram_bytes = bytes;
    } else if (name == "--transfer-rtc") {
        return select(name, value, port.transfer.clock, {{"on", true}, {"off", false}}, error);
    } else if (name == "--transfer-rumble") {
        return select(name, value, port.transfer.rumble, {{"on", true}, {"off", false}}, error);
    }
    return true;
}

bool set_option(std::string_view name, std::string_view value, Options& options, std::string& error) {
    if (name == "--pif")
        options.pif = argument_path(value);
    else if (name == "--save-file")
        options.save_file = argument_path(value);
    else if (name == "--rtc-file")
        options.rtc_file = argument_path(value);
    else if (name == "--max-instructions") {
        if (!number(name, value, options.max_instructions, error))
            return false;
        if (options.max_instructions == 0) {
            error = "The instruction limit must be positive.";
            return false;
        }
    } else if (name == "--ram") {
        if (!number(name, value, options.ram_mib, error))
            return false;
        if (options.ram_mib != 4 && options.ram_mib != 8) {
            error = "--ram must select 4 or 8 MiB.";
            return false;
        }
    } else if (name == "--region" || name == "--pif-region") {
        auto& region = name == "--region" ? options.region : options.pif_region;
        if (value == "auto")
            region.reset();
        else
            return select(name, value, region, {{"ntsc", VideoStandard::Ntsc}, {"pal", VideoStandard::Pal}},
                          error);
    } else if (name == "--save") {
        if (value == "sram96" || value == "sram128") {
            options.save = SaveType::Sram;
            options.sram_bytes = value == "sram96" ? 96U * 1024U : 128U * 1024U;
        } else {
            return select(name, value, options.save,
                          {{"none", SaveType::None},
                           {"sram32", SaveType::Sram},
                           {"flash", SaveType::FlashRam},
                           {"eeprom4k", SaveType::Eeprom4K},
                           {"eeprom16k", SaveType::Eeprom16K}},
                          error);
        }
    } else if (name == "--flash-chip") {
        return select(name, value, options.flash_chip,
                      {{"mx29l0000", FlashChip::Mx29L0000},
                       {"mx29l0001", FlashChip::Mx29L0001},
                       {"mx29l1100", FlashChip::Mx29L1100},
                       {"mx29l1101a", FlashChip::Mx29L1101A},
                       {"mx29l1101b", FlashChip::Mx29L1101B},
                       {"mx29l1101c", FlashChip::Mx29L1101C},
                       {"mn63f81mpn", FlashChip::Mn63F81Mpn}},
                      error);
    } else if (name == "--cic") {
        if (value == "auto")
            options.cic.reset();
        else
            return select(name, value, options.cic,
                          {{"6101", CicModel::Nus6101},
                           {"6102", CicModel::Nus6102},
                           {"7102", CicModel::Nus7102},
                           {"6103", CicModel::Nus6103},
                           {"6105", CicModel::Nus6105},
                           {"6106", CicModel::Nus6106},
                           {"5101", CicModel::Nus5101},
                           {"5167", CicModel::Nus5167},
                           {"8303", CicModel::Nus8303},
                           {"8401", CicModel::Nus8401},
                           {"ddus", CicModel::NusDDUS}},
                          error);
    } else {
        error = "Unrecognized option: " + std::string(name);
        return false;
    }
    return true;
}

bool validate(Options& options, std::string& error) {
    if (options.cartridge.empty() || options.pif.empty()) {
        error = "Supply a cartridge image and --pif BOOT_ROM.";
        return false;
    }
    if (options.flash_chip && options.save != SaveType::FlashRam) {
        error = "--flash-chip requires --save flash.";
        return false;
    }
    if (options.save == SaveType::FlashRam && !options.flash_chip) {
        error = "--save flash requires an explicit --flash-chip selection.";
        return false;
    }
    if (!options.save) {
        error = "Select cartridge save hardware explicitly with --save, including --save none when no save "
                "device is present.";
        return false;
    }
    if (!options.save_file.empty() && options.save == SaveType::None) {
        error = "--save-file cannot be used with --save none.";
        return false;
    }
    if (!options.rtc_file.empty() && !options.rtc) {
        error = "--rtc-file requires --rtc.";
        return false;
    }
    for (unsigned index = 0; index < options.ports.size(); ++index) {
        auto& port = options.ports[index];
        const bool gamepad = port.controller.connected && port.controller.device == ControllerDevice::Gamepad;
        if (!gamepad && !port.accessory_selected)
            port.controller.accessory = ControllerAccessory::None;
        if (!port.transfer.cartridge.empty() && !port.accessory_selected)
            port.controller.accessory = ControllerAccessory::TransferPak;
        if (!gamepad && port.controller.accessory != ControllerAccessory::None) {
            error = "Port " + std::to_string(index + 1) + ": accessories require a connected gamepad.";
            return false;
        }
        if (!port.pak_file.empty() && port.controller.accessory != ControllerAccessory::ControllerPak) {
            error = "Port " + std::to_string(index + 1) + ": --pak-file requires a Controller Pak.";
            return false;
        }
        if (port.pak_banks_selected && port.controller.accessory != ControllerAccessory::ControllerPak) {
            error = "Port " + std::to_string(index + 1) + ": --pak-banks requires a Controller Pak.";
            return false;
        }
        if (!port.transfer.cartridge.empty() &&
            port.controller.accessory != ControllerAccessory::TransferPak) {
            error = "Port " + std::to_string(index + 1) + ": --transfer-rom requires a Transfer Pak.";
            return false;
        }
        if (port.transfer.cartridge.empty() &&
            (port.transfer.mapper || port.transfer.ram_bytes || port.transfer.clock || port.transfer.rumble ||
             !port.transfer.save_file.empty() || !port.transfer.rtc_file.empty())) {
            error = "Port " + std::to_string(index + 1) + ": Game Boy settings require --transfer-rom.";
            return false;
        }
    }
    return true;
}
} // namespace

std::filesystem::path argument_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::optional<Options> parse_options(std::span<const std::string_view> arguments, std::string& error) {
    error.clear();
    Options options;
    if (arguments.size() == 1 && arguments.front() == "--help") {
        options.help = true;
        return options;
    }
    std::unordered_set<std::string> selected;
    bool positional = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto name = arguments[index];
        if (name == "--" && !positional) {
            positional = true;
            continue;
        }
        if (positional || (!name.empty() && name.front() != '-')) {
            if (!options.cartridge.empty()) {
                error = "Only one cartridge image can be supplied.";
                return std::nullopt;
            }
            options.cartridge = argument_path(name);
            continue;
        }
        if (name == "--rtc" || name == "--require-test-success" || name == "--require-extended-tests") {
            if (!selected.insert(std::string(name)).second) {
                error = "Repeated option: " + std::string(name);
                return std::nullopt;
            }
            if (name == "--rtc")
                options.rtc = true;
            else if (name == "--require-extended-tests") {
                options.require_extended = true;
                options.require_success = true;
            } else
                options.require_success = true;
            continue;
        }
        if (index + 1 == arguments.size() || arguments[index + 1].empty()) {
            error = "Missing value for " + std::string(name) + '.';
            return std::nullopt;
        }
        auto value = arguments[++index];
        unsigned port = 0;
        std::string key(name);
        if (is_port_option(name)) {
            if (!port_value(name, value, port, value, error))
                return std::nullopt;
            key += ':' + std::to_string(port);
        }
        if (!selected.insert(key).second) {
            error = "Repeated option: " + std::string(name);
            return std::nullopt;
        }
        const bool valid = is_port_option(name) ? set_port(name, value, options.ports[port], error)
                                                : set_option(name, value, options, error);
        if (!valid)
            return std::nullopt;
    }
    if (!validate(options, error))
        return std::nullopt;
    return options;
}

std::string_view usage() {
    return "Usage: cupid-n64 CARTRIDGE --pif BOOT_ROM [options]\n"
           "  --region auto|ntsc|pal       Console video region (default: cartridge header)\n"
           "  --pif-region auto|ntsc|pal   Declare the region of unrecognized firmware\n"
           "  --ram 4|8                   Installed RAM in MiB (default: 8)\n"
           "  --cic auto|6101|6102|7102|6103|6105|6106|5101|5167|8303|8401|ddus\n"
           "  --save none|sram32|sram96|sram128|flash|eeprom4k|eeprom16k\n"
           "  --flash-chip mx29l0000|mx29l0001|mx29l1100|mx29l1101a|mx29l1101b|mx29l1101c|mn63f81mpn\n"
           "  --save-file FILE            Raw cartridge save image\n"
           "  --rtc                       Attach a cartridge real-time clock\n"
           "  --rtc-file FILE             Raw 32-byte cartridge RTC register image\n"
           "  --controller PORT:gamepad|mouse|none\n"
           "  --accessory PORT:none|controller-pak|rumble-pak|bio-sensor|transfer-pak\n"
           "  --pak-file PORT:FILE        Raw Controller Pak image\n"
           "  --pak-banks PORT:COUNT      Controller Pak banks, 1 through 62 (default: 1)\n"
           "  --transfer-rom PORT:FILE    Game Boy cartridge in a Transfer Pak\n"
           "  --transfer-mapper PORT:linear|mbc1|mbc2|mbc3|mbc30|mbc5\n"
           "  --transfer-ram PORT:BYTES   Override Game Boy RAM capacity\n"
           "  --transfer-rtc PORT:on|off  Override Game Boy clock presence\n"
           "  --transfer-rtc-file PORT:FILE Persist Game Boy RTC state\n"
           "  --transfer-rumble PORT:on|off\n"
           "  --transfer-save PORT:FILE   Raw Game Boy cartridge RAM\n"
           "  --max-instructions COUNT   Positive instruction limit (default: 4000000000)\n"
           "  --require-test-success     Fail unless the test ROM completes without failures\n"
           "  --require-extended-tests   Require all extended test categories and imply test success\n"
           "Ports are numbered 1 through 4. Put -- before a cartridge path beginning with -.\n";
}
} // namespace cupid::host
