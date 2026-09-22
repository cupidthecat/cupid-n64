#include "cupid/host/preferences.hpp"

#include "cupid/host/storage.hpp"
#include "cupid/storage/file.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cupid::host {
namespace detail {
std::array<u8, 32> preferences_sha256(std::span<const u8> data);
}
namespace {

using namespace std::string_view_literals;

constexpr std::size_t max_preferences_bytes = 512U * 1024U;
constexpr std::size_t max_string_bytes = 16U * 1024U;
constexpr unsigned preferences_version = 2;

bool valid_utf8_path_text(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<u8>(text[index]);
        if (first == 0)
            return false;
        if (first < 0x80U) {
            ++index;
            continue;
        }

        unsigned continuation = 0;
        u32 code_point = 0;
        u32 minimum = 0;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1;
            code_point = first & 0x1fU;
            minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2;
            code_point = first & 0x0fU;
            minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3;
            code_point = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuation >= text.size())
            return false;
        for (unsigned offset = 1; offset <= continuation; ++offset) {
            const auto next = static_cast<u8>(text[index + offset]);
            if ((next & 0xc0U) != 0x80U)
                return false;
            code_point = (code_point << 6U) | (next & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU))
            return false;
        index += continuation + 1U;
    }
    return true;
}

bool validate_path_text(std::string_view text, std::string_view name, std::string& error) {
    if (!valid_utf8_path_text(text)) {
        error = std::string(name) + " must be valid UTF-8 and contain no embedded NUL byte.";
        return false;
    }
    if (text.size() > max_string_bytes) {
        error = std::string(name) + " exceeds the 16 KiB preferences string limit.";
        return false;
    }
    return true;
}

std::string quote_text(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.push_back('"');
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20U || byte == 0x7fU) {
                result += "\\x";
                result.push_back(hex[byte >> 4U]);
                result.push_back(hex[byte & 15U]);
            } else {
                result.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

bool digit(char value, unsigned& result) {
    if (value >= '0' && value <= '9') {
        result = static_cast<unsigned>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        result = static_cast<unsigned>(value - 'a') + 10U;
        return true;
    }
    if (value >= 'A' && value <= 'F') {
        result = static_cast<unsigned>(value - 'A') + 10U;
        return true;
    }
    return false;
}

bool parse_quoted(std::string_view text, std::string& output) {
    if (text.size() < 2 || text.front() != '"')
        return false;
    std::string result;
    for (std::size_t index = 1; index < text.size(); ++index) {
        const char value = text[index];
        if (value == '"') {
            if (index + 1 != text.size())
                return false;
            output = std::move(result);
            return true;
        }
        if (value != '\\') {
            result.push_back(value);
        } else {
            if (++index >= text.size())
                return false;
            switch (text[index]) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'x': {
                if (index + 2 >= text.size())
                    return false;
                unsigned high = 0, low = 0;
                if (!digit(text[index + 1], high) || !digit(text[index + 2], low))
                    return false;
                result.push_back(static_cast<char>((high << 4U) | low));
                index += 2;
                break;
            }
            default:
                return false;
            }
        }
        if (result.size() > max_string_bytes)
            return false;
    }
    return false;
}

template <typename T> bool parse_unsigned(std::string_view text, T& result) {
    T value{};
    const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (status != std::errc{} || end != text.data() + text.size())
        return false;
    result = value;
    return true;
}

bool parse_bool(std::string_view text, bool& value) {
    if (text == "0") {
        value = false;
        return true;
    }
    if (text == "1") {
        value = true;
        return true;
    }
    return false;
}

template <typename T, std::size_t N>
bool parse_enum(std::string_view text, const std::array<std::pair<std::string_view, T>, N>& names, T& value) {
    for (const auto& [name, candidate] : names) {
        if (text == name) {
            value = candidate;
            return true;
        }
    }
    return false;
}

template <typename T, std::size_t N>
bool parse_optional_enum(std::string_view text, const std::array<std::pair<std::string_view, T>, N>& names,
                         std::optional<T>& value) {
    if (text == "auto") {
        value.reset();
        return true;
    }
    T parsed{};
    if (!parse_enum(text, names, parsed))
        return false;
    value = parsed;
    return true;
}

template <typename T, std::size_t N>
std::string_view enum_name(T value, const std::array<std::pair<std::string_view, T>, N>& names) {
    for (const auto& [name, candidate] : names) {
        if (candidate == value)
            return name;
    }
    return {};
}

template <typename T, std::size_t N>
std::string_view optional_enum_name(const std::optional<T>& value,
                                    const std::array<std::pair<std::string_view, T>, N>& names) {
    return value ? enum_name(*value, names) : std::string_view{"auto"};
}

constexpr std::array region_names{std::pair{"ntsc"sv, VideoStandard::Ntsc},
                                  std::pair{"pal"sv, VideoStandard::Pal}};
constexpr std::array save_names{std::pair{"none"sv, SaveType::None}, std::pair{"sram"sv, SaveType::Sram},
                                std::pair{"flash"sv, SaveType::FlashRam},
                                std::pair{"eeprom4k"sv, SaveType::Eeprom4K},
                                std::pair{"eeprom16k"sv, SaveType::Eeprom16K}};
constexpr std::array flash_names{
    std::pair{"mx29l0000"sv, FlashChip::Mx29L0000},   std::pair{"mx29l0001"sv, FlashChip::Mx29L0001},
    std::pair{"mx29l1100"sv, FlashChip::Mx29L1100},   std::pair{"mx29l1101a"sv, FlashChip::Mx29L1101A},
    std::pair{"mx29l1101b"sv, FlashChip::Mx29L1101B}, std::pair{"mx29l1101c"sv, FlashChip::Mx29L1101C},
    std::pair{"mn63f81mpn"sv, FlashChip::Mn63F81Mpn}};
constexpr std::array cic_names{std::pair{"6101"sv, CicModel::Nus6101}, std::pair{"6102"sv, CicModel::Nus6102},
                               std::pair{"7102"sv, CicModel::Nus7102}, std::pair{"6103"sv, CicModel::Nus6103},
                               std::pair{"6105"sv, CicModel::Nus6105}, std::pair{"6106"sv, CicModel::Nus6106},
                               std::pair{"5101"sv, CicModel::Nus5101}, std::pair{"5167"sv, CicModel::Nus5167},
                               std::pair{"8303"sv, CicModel::Nus8303}, std::pair{"8401"sv, CicModel::Nus8401},
                               std::pair{"ddus"sv, CicModel::NusDDUS}};
constexpr std::array device_names{std::pair{"gamepad"sv, ControllerDevice::Gamepad},
                                  std::pair{"mouse"sv, ControllerDevice::Mouse},
                                  std::pair{"gamecube"sv, ControllerDevice::GameCube}};
constexpr std::array accessory_names{std::pair{"none"sv, ControllerAccessory::None},
                                     std::pair{"controller-pak"sv, ControllerAccessory::ControllerPak},
                                     std::pair{"rumble-pak"sv, ControllerAccessory::RumblePak},
                                     std::pair{"bio-sensor"sv, ControllerAccessory::BioSensor},
                                     std::pair{"transfer-pak"sv, ControllerAccessory::TransferPak}};
constexpr std::array mapper_names{
    std::pair{"linear"sv, GameBoyMapper::Linear}, std::pair{"mbc1"sv, GameBoyMapper::Mbc1},
    std::pair{"mbc2"sv, GameBoyMapper::Mbc2},     std::pair{"mbc3"sv, GameBoyMapper::Mbc3},
    std::pair{"mbc30"sv, GameBoyMapper::Mbc30},   std::pair{"mbc5"sv, GameBoyMapper::Mbc5}};

class PayloadParser {
  public:
    explicit PayloadParser(std::string_view payload) : payload_(payload) {}

    bool string(std::string_view key, std::string& value, std::string& error) {
        std::string_view encoded;
        if (!field(key, encoded, error) || !parse_quoted(encoded, value)) {
            if (error.empty())
                error = "Invalid quoted value for " + std::string(key) + '.';
            return false;
        }
        return true;
    }

    bool path(std::string_view key, std::filesystem::path& value, std::string& error) {
        std::string text;
        if (!string(key, text, error))
            return false;
        if (!validate_path_text(text, key, error))
            return false;
        try {
            value = argument_path(text);
        } catch (const std::exception& exception) {
            error = "Invalid UTF-8 path for " + std::string(key) + ": " + exception.what();
            return false;
        }
        return true;
    }

    bool boolean(std::string_view key, bool& value, std::string& error) {
        std::string_view text;
        if (!field(key, text, error) || !parse_bool(text, value)) {
            if (error.empty())
                error = "Invalid boolean value for " + std::string(key) + '.';
            return false;
        }
        return true;
    }

    template <typename T> bool number(std::string_view key, T& value, std::string& error) {
        std::string_view text;
        if (!field(key, text, error) || !parse_unsigned(text, value)) {
            if (error.empty())
                error = "Invalid numeric value for " + std::string(key) + '.';
            return false;
        }
        return true;
    }

    template <typename T, std::size_t N>
    bool enumeration(std::string_view key, const std::array<std::pair<std::string_view, T>, N>& names,
                     T& value, std::string& error) {
        std::string_view text;
        if (!field(key, text, error) || !parse_enum(text, names, value)) {
            if (error.empty())
                error = "Invalid enum value for " + std::string(key) + '.';
            return false;
        }
        return true;
    }

    template <typename T, std::size_t N>
    bool optional_enumeration(std::string_view key,
                              const std::array<std::pair<std::string_view, T>, N>& names,
                              std::optional<T>& value, std::string& error) {
        std::string_view text;
        if (!field(key, text, error) || !parse_optional_enum(text, names, value)) {
            if (error.empty())
                error = "Invalid enum value for " + std::string(key) + '.';
            return false;
        }
        return true;
    }

    bool optional_bool(std::string_view key, std::optional<bool>& value, std::string& error) {
        std::string_view text;
        if (!field(key, text, error))
            return false;
        if (text == "auto") {
            value.reset();
            return true;
        }
        bool parsed = false;
        if (!parse_bool(text, parsed)) {
            error = "Invalid optional boolean value for " + std::string(key) + '.';
            return false;
        }
        value = parsed;
        return true;
    }

    bool optional_number(std::string_view key, std::optional<unsigned>& value, std::string& error) {
        std::string_view text;
        if (!field(key, text, error))
            return false;
        if (text == "auto") {
            value.reset();
            return true;
        }
        unsigned parsed = 0;
        if (!parse_unsigned(text, parsed)) {
            error = "Invalid optional numeric value for " + std::string(key) + '.';
            return false;
        }
        value = parsed;
        return true;
    }

    bool finished(std::string& error) const {
        if (position_ == payload_.size())
            return true;
        error = "Preferences payload contains unexpected trailing fields.";
        return false;
    }

  private:
    bool field(std::string_view key, std::string_view& value, std::string& error) {
        if (position_ >= payload_.size()) {
            error = "Preferences payload ended before " + std::string(key) + '.';
            return false;
        }
        const auto newline = payload_.find('\n', position_);
        if (newline == std::string_view::npos) {
            error = "Preferences field " + std::string(key) + " is not newline terminated.";
            return false;
        }
        const auto line = payload_.substr(position_, newline - position_);
        position_ = newline + 1;
        if (!line.starts_with(key) || line.size() <= key.size() || line[key.size()] != '=') {
            error = "Expected preferences field " + std::string(key) + '.';
            return false;
        }
        value = line.substr(key.size() + 1);
        return true;
    }

    std::string_view payload_;
    std::size_t position_{};
};

std::string path_value(const std::filesystem::path& path) {
    return quote_text(path_text(path));
}

void line(std::string& payload, std::string_view key, std::string_view value) {
    payload += key;
    payload.push_back('=');
    payload += value;
    payload.push_back('\n');
}

void line(std::string& payload, std::string_view key, bool value) {
    line(payload, key, std::string_view(value ? "1" : "0"));
}

template <typename T> void line_number(std::string& payload, std::string_view key, T value) {
    line(payload, key, std::to_string(value));
}

bool validate_path(const std::filesystem::path& path, std::string_view name, std::string& error) {
    try {
        return validate_path_text(path_text(path), name, error);
    } catch (const std::exception& exception) {
        error = "Unable to encode " + std::string(name) + " as UTF-8: " + exception.what();
        return false;
    }
}

std::filesystem::path normalized_path(const std::filesystem::path& path) {
    std::error_code code;
    auto normalized = std::filesystem::weakly_canonical(path, code);
    if (!code)
        return normalized;
    normalized = std::filesystem::absolute(path, code);
    return code ? path.lexically_normal() : normalized.lexically_normal();
}

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code code;
    if (std::filesystem::exists(left, code) && !code && std::filesystem::exists(right, code) && !code) {
        const bool equivalent = std::filesystem::equivalent(left, right, code);
        if (!code)
            return equivalent;
    }
    const auto a = normalized_path(left);
    const auto b = normalized_path(right);
#ifdef _WIN32
    return CompareStringOrdinal(a.native().c_str(), -1, b.native().c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return a == b;
#endif
}

bool validate_preferences_destination(const std::filesystem::path& path, const Preferences& preferences,
                                      std::string& error) {
    if (path.empty())
        return true;
    const auto reject_collision = [&](std::string_view name, const std::filesystem::path& candidate) {
        if (candidate.empty() || !same_path(path, candidate))
            return false;
        error = "Preferences file collides with " + std::string(name) + ": " + path_text(path);
        return true;
    };

    const auto& hardware = preferences.hardware;
    if (reject_collision("the cartridge ROM", hardware.cartridge) ||
        reject_collision("the PIF ROM", hardware.pif) ||
        reject_collision("cartridge save storage", hardware.save_file) ||
        reject_collision("cartridge RTC storage", hardware.rtc_file))
        return false;
    for (unsigned index = 0; index < hardware.ports.size(); ++index) {
        const auto& port = hardware.ports[index];
        const auto port_name = "port " + std::to_string(index + 1) + ' ';
        if (reject_collision(port_name + "Controller Pak storage", port.pak_file) ||
            reject_collision(port_name + "Transfer Pak ROM", port.transfer.cartridge) ||
            reject_collision(port_name + "Transfer Pak RAM storage", port.transfer.save_file) ||
            reject_collision(port_name + "Transfer Pak RTC storage", port.transfer.rtc_file))
            return false;
    }
    return true;
}

bool validate_preferences(const Preferences& preferences, std::string& error) {
    if (!std::isfinite(preferences.volume) || preferences.volume < 0.0F || preferences.volume > 1.0F) {
        error = "Volume must be finite and between 0 and 1.";
        return false;
    }
    if (preferences.input_bindings.size() > max_string_bytes) {
        error = "Input bindings exceed the 16 KiB preferences limit.";
        return false;
    }
    const auto& hardware = preferences.hardware;
    if (hardware.ram_mib != 4 && hardware.ram_mib != 8) {
        error = "Installed RAM must be 4 or 8 MiB.";
        return false;
    }
    if (hardware.sram_bytes != 32U * 1024U && hardware.sram_bytes != 96U * 1024U &&
        hardware.sram_bytes != 128U * 1024U) {
        error = "SRAM capacity must be 32, 96, or 128 KiB.";
        return false;
    }
    const std::array<std::pair<std::string_view, const std::filesystem::path*>, 4> global_paths{
        {{"cartridge path", &hardware.cartridge},
         {"PIF path", &hardware.pif},
         {"save path", &hardware.save_file},
         {"RTC path", &hardware.rtc_file}}};
    for (const auto& [name, path] : global_paths) {
        if (!validate_path(*path, name, error))
            return false;
    }
    if ((hardware.region && enum_name(*hardware.region, region_names).empty()) ||
        (hardware.pif_region && enum_name(*hardware.pif_region, region_names).empty()) ||
        (hardware.save && enum_name(*hardware.save, save_names).empty()) ||
        (hardware.flash_chip && enum_name(*hardware.flash_chip, flash_names).empty()) ||
        (hardware.cic && enum_name(*hardware.cic, cic_names).empty())) {
        error = "Hardware preferences contain an invalid enum value.";
        return false;
    }
    if (hardware.flash_chip && hardware.save != SaveType::FlashRam) {
        error = "Flash chip selection requires FlashRAM save hardware.";
        return false;
    }
    if (hardware.save == SaveType::FlashRam && !hardware.flash_chip) {
        error = "FlashRAM preferences require an explicit flash chip.";
        return false;
    }
    if (hardware.save == SaveType::None && !hardware.save_file.empty()) {
        error = "A save path cannot be stored with no save hardware.";
        return false;
    }
    if (!hardware.rtc && !hardware.rtc_file.empty()) {
        error = "An RTC path requires cartridge RTC hardware.";
        return false;
    }
    for (unsigned index = 0; index < hardware.ports.size(); ++index) {
        const auto& port = hardware.ports[index];
        if (port.pak_banks < 1 || port.pak_banks > 62) {
            error = "Controller Pak capacity must be between one and 62 banks on port " +
                    std::to_string(index + 1) + '.';
            return false;
        }
        if (enum_name(port.controller.device, device_names).empty() ||
            enum_name(port.controller.accessory, accessory_names).empty()) {
            error = "Controller preferences contain an invalid enum value on port " +
                    std::to_string(index + 1) + '.';
            return false;
        }
        if (!validate_path(port.pak_file, "Controller Pak path", error) ||
            !validate_path(port.transfer.cartridge, "Transfer Pak cartridge path", error) ||
            !validate_path(port.transfer.save_file, "Transfer Pak save path", error) ||
            !validate_path(port.transfer.rtc_file, "Transfer Pak RTC path", error))
            return false;
        if (port.transfer.mapper && enum_name(*port.transfer.mapper, mapper_names).empty()) {
            error = "Transfer Pak preferences contain an invalid mapper enum.";
            return false;
        }
        if (port.transfer.ram_bytes && *port.transfer.ram_bytes > 128U * 1024U) {
            error = "Transfer Pak RAM capacity exceeds 128 KiB.";
            return false;
        }
    }
    return true;
}

std::string optional_bool_value(const std::optional<bool>& value) {
    return value ? (*value ? "1" : "0") : "auto";
}

std::string optional_number_value(const std::optional<unsigned>& value) {
    return value ? std::to_string(*value) : "auto";
}

std::string serialize_payload(const Preferences& preferences) {
    const auto& hardware = preferences.hardware;
    std::string payload;
    line_number(payload, "volume_millionths",
                static_cast<unsigned>(std::lround(preferences.volume * 1'000'000.0F)));
    line(payload, "muted", preferences.muted);
    line(payload, "input_bindings", quote_text(preferences.input_bindings));
    line(payload, "cartridge", path_value(hardware.cartridge));
    line(payload, "pif", path_value(hardware.pif));
    line(payload, "save_file", path_value(hardware.save_file));
    line(payload, "rtc_file", path_value(hardware.rtc_file));
    line(payload, "region", optional_enum_name(hardware.region, region_names));
    line(payload, "pif_region", optional_enum_name(hardware.pif_region, region_names));
    line_number(payload, "ram_mib", hardware.ram_mib);
    line(payload, "save", optional_enum_name(hardware.save, save_names));
    line_number(payload, "sram_bytes", hardware.sram_bytes);
    line(payload, "flash_chip", optional_enum_name(hardware.flash_chip, flash_names));
    line(payload, "cic", optional_enum_name(hardware.cic, cic_names));
    line(payload, "rtc", hardware.rtc);
    for (unsigned index = 0; index < hardware.ports.size(); ++index) {
        const auto prefix = "port" + std::to_string(index + 1) + '_';
        const auto& port = hardware.ports[index];
        line(payload, prefix + "connected", port.controller.connected);
        line(payload, prefix + "device", enum_name(port.controller.device, device_names));
        line(payload, prefix + "accessory", enum_name(port.controller.accessory, accessory_names));
        line(payload, prefix + "controller_selected", port.controller_selected);
        line(payload, prefix + "accessory_selected", port.accessory_selected);
        line(payload, prefix + "pak_banks_selected", port.pak_banks_selected);
        line_number(payload, prefix + "pak_banks", port.pak_banks);
        line(payload, prefix + "pak_file", path_value(port.pak_file));
        line(payload, prefix + "transfer_rom", path_value(port.transfer.cartridge));
        line(payload, prefix + "transfer_save", path_value(port.transfer.save_file));
        line(payload, prefix + "transfer_rtc_file", path_value(port.transfer.rtc_file));
        line(payload, prefix + "transfer_mapper", optional_enum_name(port.transfer.mapper, mapper_names));
        line(payload, prefix + "transfer_ram", optional_number_value(port.transfer.ram_bytes));
        line(payload, prefix + "transfer_rtc", optional_bool_value(port.transfer.clock));
        line(payload, prefix + "transfer_rumble", optional_bool_value(port.transfer.rumble));
    }
    return payload;
}

bool parse_payload(std::string_view payload, unsigned version, Preferences& preferences, std::string& error) {
    PayloadParser parser(payload);
    Preferences parsed;
    unsigned volume = 0;
    if (!parser.number("volume_millionths", volume, error) || volume > 1'000'000U) {
        if (error.empty())
            error = "Volume must be between 0 and 1,000,000 millionths.";
        return false;
    }
    parsed.volume = static_cast<float>(volume) / 1'000'000.0F;
    if (!parser.boolean("muted", parsed.muted, error) ||
        !parser.string("input_bindings", parsed.input_bindings, error) ||
        !parser.path("cartridge", parsed.hardware.cartridge, error) ||
        !parser.path("pif", parsed.hardware.pif, error) ||
        !parser.path("save_file", parsed.hardware.save_file, error) ||
        !parser.path("rtc_file", parsed.hardware.rtc_file, error) ||
        !parser.optional_enumeration("region", region_names, parsed.hardware.region, error) ||
        !parser.optional_enumeration("pif_region", region_names, parsed.hardware.pif_region, error) ||
        !parser.number("ram_mib", parsed.hardware.ram_mib, error) ||
        !parser.optional_enumeration("save", save_names, parsed.hardware.save, error) ||
        !parser.number("sram_bytes", parsed.hardware.sram_bytes, error) ||
        !parser.optional_enumeration("flash_chip", flash_names, parsed.hardware.flash_chip, error) ||
        !parser.optional_enumeration("cic", cic_names, parsed.hardware.cic, error) ||
        !parser.boolean("rtc", parsed.hardware.rtc, error))
        return false;
    for (unsigned index = 0; index < parsed.hardware.ports.size(); ++index) {
        const auto prefix = "port" + std::to_string(index + 1) + '_';
        auto& port = parsed.hardware.ports[index];
        if (!parser.boolean(prefix + "connected", port.controller.connected, error) ||
            !parser.enumeration(prefix + "device", device_names, port.controller.device, error) ||
            !parser.enumeration(prefix + "accessory", accessory_names, port.controller.accessory, error) ||
            !parser.boolean(prefix + "controller_selected", port.controller_selected, error) ||
            !parser.boolean(prefix + "accessory_selected", port.accessory_selected, error))
            return false;
        if (version >= 2 && (!parser.boolean(prefix + "pak_banks_selected", port.pak_banks_selected, error) ||
                             !parser.number(prefix + "pak_banks", port.pak_banks, error)))
            return false;
        if (!parser.path(prefix + "pak_file", port.pak_file, error) ||
            !parser.path(prefix + "transfer_rom", port.transfer.cartridge, error) ||
            !parser.path(prefix + "transfer_save", port.transfer.save_file, error) ||
            !parser.path(prefix + "transfer_rtc_file", port.transfer.rtc_file, error) ||
            !parser.optional_enumeration(prefix + "transfer_mapper", mapper_names, port.transfer.mapper,
                                         error) ||
            !parser.optional_number(prefix + "transfer_ram", port.transfer.ram_bytes, error) ||
            !parser.optional_bool(prefix + "transfer_rtc", port.transfer.clock, error) ||
            !parser.optional_bool(prefix + "transfer_rumble", port.transfer.rumble, error))
            return false;
        port.controller.buttons = 0;
        port.controller.stick_x = 0;
        port.controller.stick_y = 0;
    }
    if (!parser.finished(error) || !validate_preferences(parsed, error))
        return false;
    preferences = std::move(parsed);
    return true;
}

std::string digest_hex(std::span<const u8> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    const auto digest = detail::preferences_sha256(bytes);
    std::string result;
    result.reserve(digest.size() * 2U);
    for (const u8 byte : digest) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 15U]);
    }
    return result;
}

bool ensure_content_directory(const std::filesystem::path& directory, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    if (code) {
        error =
            "Unable to create automatic storage directory " + path_text(directory) + ": " + code.message();
        return false;
    }
    if (!std::filesystem::is_directory(directory, code) || code) {
        error = "Automatic storage path is not a directory: " + path_text(directory);
        if (code)
            error += " (" + code.message() + ')';
        return false;
    }
    return true;
}

std::string cartridge_extension(SaveType save) {
    switch (save) {
    case SaveType::Sram:
        return ".sra";
    case SaveType::FlashRam:
        return ".fla";
    case SaveType::Eeprom4K:
    case SaveType::Eeprom16K:
        return ".eep";
    case SaveType::None:
        return {};
    }
    return {};
}

} // namespace

bool load_preferences(const std::filesystem::path& path, Preferences& preferences, std::string& error) {
    error.clear();
    if (!validate_path(path, "Preferences path", error))
        return false;
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) {
        error = "Unable to inspect preferences file " + path_text(path) + ": " + code.message();
        return false;
    }
    if (!exists)
        return true;
    if (!std::filesystem::is_regular_file(path, code) || code) {
        error = "Preferences path is not a regular file: " + path_text(path);
        return false;
    }
    const auto size = std::filesystem::file_size(path, code);
    if (code || size > max_preferences_bytes) {
        error = "Preferences file exceeds the 512 KiB limit: " + path_text(path);
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to open preferences file: " + path_text(path);
        return false;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        error = "Preferences file changed or could not be read completely: " + path_text(path);
        return false;
    }
    const auto newline = bytes.find('\n');
    if (newline == std::string::npos) {
        error = "Preferences file has no complete version header.";
        return false;
    }
    const auto header = std::string_view(bytes).substr(0, newline);
    constexpr std::string_view prefix = "CUPID-PREFERENCES ";
    if (!header.starts_with(prefix)) {
        error = "Preferences file has an invalid magic header.";
        return false;
    }
    const auto space = header.find(' ', prefix.size());
    if (space == std::string_view::npos) {
        error = "Preferences file has an invalid version header.";
        return false;
    }
    unsigned version = 0;
    std::size_t payload_length = 0;
    if (!parse_unsigned(header.substr(prefix.size(), space - prefix.size()), version) ||
        !parse_unsigned(header.substr(space + 1), payload_length)) {
        error = "Preferences file has an invalid version or payload length.";
        return false;
    }
    if (version < 1 || version > preferences_version) {
        error = "Unsupported preferences version " + std::to_string(version) + '.';
        return false;
    }
    const auto payload = std::string_view(bytes).substr(newline + 1);
    if (payload_length != payload.size()) {
        error = "Preferences payload length does not match the version header.";
        return false;
    }
    Preferences parsed = preferences;
    if (!parse_payload(payload, version, parsed, error))
        return false;
    preferences = std::move(parsed);
    return true;
}

bool save_preferences(const std::filesystem::path& path, const Preferences& preferences, std::string& error) {
    error.clear();
    if (!validate_path(path, "Preferences path", error) || !validate_preferences(preferences, error) ||
        !validate_preferences_destination(path, preferences, error))
        return false;
    const auto payload = serialize_payload(preferences);
    const std::string header = "CUPID-PREFERENCES " + std::to_string(preferences_version) + ' ' +
                               std::to_string(payload.size()) + '\n';
    if (header.size() > max_preferences_bytes || payload.size() > max_preferences_bytes - header.size()) {
        error = "Serialized preferences file exceeds the 512 KiB limit.";
        return false;
    }
    std::string bytes = header + payload;
    return cupid::storage::replace_file(
        path, std::span<const u8>(reinterpret_cast<const u8*>(bytes.data()), bytes.size()), error);
}

bool prepare_storage_paths(const System& system, Options& options, const std::filesystem::path& storage_root,
                           std::string& error) {
    error.clear();
    if (system.bus.rom.empty()) {
        error = "Cannot prepare automatic storage paths without a normalized cartridge image.";
        return false;
    }
    Options prepared = options;
    bool needs_directory = (system.bus.save_type != SaveType::None && prepared.save_file.empty()) ||
                           (system.bus.rtc.has_value() && prepared.rtc_file.empty());
    for (unsigned index = 0; index < prepared.ports.size(); ++index) {
        const auto& controller = system.bus.controllers()[index];
        if (controller.connected && controller.device == ControllerDevice::Gamepad &&
            controller.accessory == ControllerAccessory::ControllerPak &&
            prepared.ports[index].pak_file.empty())
            needs_directory = true;
        const auto* cartridge = system.bus.transfer_paks[index].cartridge();
        if (cartridge && ((!cartridge->ram().empty() && prepared.ports[index].transfer.save_file.empty()) ||
                          (cartridge->config().clock && prepared.ports[index].transfer.rtc_file.empty())))
            needs_directory = true;
    }
    if (!needs_directory)
        return validate_storage_paths(prepared, error);
    if (storage_root.empty()) {
        error = "Automatic storage requires a non-empty caller-provided storage root.";
        return false;
    }
    const auto directory = storage_root / ("n64-" + digest_hex(system.bus.rom));
    if (!ensure_content_directory(directory, error))
        return false;

    if (system.bus.save_type != SaveType::None && prepared.save_file.empty())
        prepared.save_file = directory / ("cartridge" + cartridge_extension(system.bus.save_type));
    if (system.bus.rtc && prepared.rtc_file.empty())
        prepared.rtc_file = directory / "cartridge.rtc";

    for (unsigned index = 0; index < prepared.ports.size(); ++index) {
        const auto& controller = system.bus.controllers()[index];
        auto& port = prepared.ports[index];
        if (controller.connected && controller.device == ControllerDevice::Gamepad &&
            controller.accessory == ControllerAccessory::ControllerPak && port.pak_file.empty())
            port.pak_file = directory / ("controller-pak-" + std::to_string(index + 1) + ".pak");

        const auto* cartridge = system.bus.transfer_paks[index].cartridge();
        if (!cartridge)
            continue;
        const bool needs_ram = !cartridge->ram().empty() && port.transfer.save_file.empty();
        const bool needs_clock = cartridge->config().clock && port.transfer.rtc_file.empty();
        if (!needs_ram && !needs_clock)
            continue;
        const std::string identity = digest_hex(cartridge->rom());
        const std::string prefix = "transfer-" + std::to_string(index + 1) + '-' + identity;
        if (needs_ram)
            port.transfer.save_file = directory / (prefix + ".sav");
        if (needs_clock)
            port.transfer.rtc_file = directory / (prefix + ".gbrt");
    }

    if (!validate_storage_paths(prepared, error))
        return false;
    options = std::move(prepared);
    return true;
}

} // namespace cupid::host
