#include "cupid/host/storage.hpp"

#include "cupid/storage/file.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cupid::host {
namespace {

struct NamedPath {
    std::string_view name;
    const std::filesystem::path* path;
};

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

std::vector<NamedPath> storage_paths(const Options& options) {
    std::vector<NamedPath> paths;
    paths.push_back({"cartridge save", &options.save_file});
    paths.push_back({"cartridge RTC", &options.rtc_file});
    for (unsigned port = 0; port < options.ports.size(); ++port) {
        const auto& current = options.ports[port];
        if (!current.pak_file.empty())
            paths.push_back({"Controller Pak", &current.pak_file});
        if (!current.transfer.save_file.empty())
            paths.push_back({"Transfer Pak RAM", &current.transfer.save_file});
        if (!current.transfer.rtc_file.empty())
            paths.push_back({"Transfer Pak RTC", &current.transfer.rtc_file});
    }
    return paths;
}

std::vector<NamedPath> source_paths(const Options& options) {
    std::vector<NamedPath> paths{{"cartridge ROM", &options.cartridge}, {"PIF ROM", &options.pif}};
    for (const auto& port : options.ports) {
        if (!port.transfer.cartridge.empty())
            paths.push_back({"Transfer Pak ROM", &port.transfer.cartridge});
    }
    return paths;
}

bool read_exact(const std::filesystem::path& path, std::span<u8> output, std::string_view label,
                std::string& error) {
    if (path.empty())
        return true;
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code) {
        error =
            "Unable to inspect " + std::string(label) + " file " + path_text(path) + ": " + code.message();
        return false;
    }
    if (!exists)
        return true;
    if (!std::filesystem::is_regular_file(path, code) || code) {
        error = std::string(label) + " path is not a regular file: " + path_text(path);
        return false;
    }
    const auto size = std::filesystem::file_size(path, code);
    if (code || size != output.size()) {
        error = std::string(label) + " file must contain exactly " + std::to_string(output.size()) +
                " bytes: " + path_text(path);
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to open " + std::string(label) + " file: " + path_text(path);
        return false;
    }
    std::vector<u8> bytes(output.size());
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        error = std::string(label) + " file changed or could not be read completely: " + path_text(path);
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return true;
}

bool atomic_replace(const std::filesystem::path& path, std::span<const u8> bytes, std::string_view label,
                    std::string& error) {
    if (path.empty())
        return true;
    std::string storage_error;
    if (cupid::storage::replace_file(path, bytes, storage_error))
        return true;
    error = std::string(label) + " file " + path_text(path) + ": " + storage_error;
    return false;
}

std::span<u8> cartridge_save(System& system) {
    switch (system.bus.save_type) {
    case SaveType::Sram:
        return system.bus.sram;
    case SaveType::FlashRam:
        return system.bus.flashram;
    case SaveType::Eeprom4K:
    case SaveType::Eeprom16K:
        return system.bus.eeprom;
    case SaveType::None:
        return {};
    }
    return {};
}

std::span<const u8> cartridge_save(const System& system) {
    switch (system.bus.save_type) {
    case SaveType::Sram:
        return system.bus.sram;
    case SaveType::FlashRam:
        return system.bus.flashram;
    case SaveType::Eeprom4K:
    case SaveType::Eeprom16K:
        return system.bus.eeprom;
    case SaveType::None:
        return {};
    }
    return {};
}

constexpr std::size_t game_boy_rtc_size = 12;

std::array<u8, game_boy_rtc_size> encode_game_boy_rtc(const GameBoyClock& clock) {
    return {'G',
            'B',
            'R',
            'T',
            1,
            clock.seconds,
            clock.minutes,
            clock.hours,
            static_cast<u8>(clock.days >> 8U),
            static_cast<u8>(clock.days),
            static_cast<u8>((clock.halted ? 1U : 0U) | (clock.carry ? 2U : 0U)),
            0};
}

bool decode_game_boy_rtc(std::span<const u8, game_boy_rtc_size> bytes, GameBoyClock& clock,
                         std::string& error) {
    if (!std::equal(bytes.begin(), bytes.begin() + 4, std::array<u8, 4>{'G', 'B', 'R', 'T'}.begin()) ||
        bytes[4] != 1 || bytes[11] != 0 || bytes[5] > 63 || bytes[6] > 63 || bytes[7] > 31 ||
        (bytes[10] & ~3U) != 0) {
        error = "Game Boy RTC file has an invalid GBRT v1 record.";
        return false;
    }
    const u16 days = static_cast<u16>((static_cast<u16>(bytes[8]) << 8U) | bytes[9]);
    if (days > 511) {
        error = "Game Boy RTC day counter is outside the supported 9-bit range.";
        return false;
    }
    clock = {bytes[5], bytes[6], bytes[7], days, (bytes[10] & 1U) != 0, (bytes[10] & 2U) != 0};
    return true;
}

bool load_game_boy_rtc(const std::filesystem::path& path, GameBoyCartridge& cartridge, std::string& error) {
    if (path.empty())
        return true;
    if (!cartridge.config().clock) {
        error = "A Transfer Pak RTC file was supplied for a cartridge without an RTC.";
        return false;
    }
    std::array<u8, game_boy_rtc_size> bytes{};
    std::error_code code;
    if (!std::filesystem::exists(path, code) && !code)
        return true;
    if (!read_exact(path, bytes, "Game Boy RTC", error))
        return false;
    GameBoyClock clock{};
    if (!decode_game_boy_rtc(bytes, clock, error))
        return false;
    cartridge.set_clock(clock);
    return true;
}

} // namespace

bool validate_storage_paths(const Options& options, std::string& error) {
    error.clear();
    const auto destinations = storage_paths(options);
    const auto sources = source_paths(options);
    for (std::size_t index = 0; index < destinations.size(); ++index) {
        const auto& destination = destinations[index];
        if (destination.path->empty())
            continue;
        for (const auto& source : sources) {
            if (!source.path->empty() && same_path(*destination.path, *source.path)) {
                error = std::string(destination.name) + " file collides with the " +
                        std::string(source.name) + ": " + path_text(*destination.path);
                return false;
            }
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (!destinations[other].path->empty() &&
                same_path(*destination.path, *destinations[other].path)) {
                error = std::string(destination.name) +
                        " file collides with another storage file: " + path_text(*destination.path);
                return false;
            }
        }
    }
    return true;
}

bool load_persistent_storage(System& system, const Options& options, std::string& error) {
    error.clear();
    auto save = cartridge_save(system);
    if (!options.save_file.empty() &&
        (save.empty() || !read_exact(options.save_file, save, "cartridge save", error)))
        return false;

    if (!options.rtc_file.empty()) {
        if (!system.bus.rtc) {
            error = "A cartridge RTC file was supplied without cartridge RTC hardware.";
            return false;
        }
        CartridgeRtc::Registers registers{};
        if (!read_exact(options.rtc_file, registers, "cartridge RTC", error))
            return false;
        std::error_code code;
        if (std::filesystem::exists(options.rtc_file, code) && !code)
            system.bus.rtc.emplace(registers);
    }

    for (unsigned port = 0; port < options.ports.size(); ++port) {
        const auto& current = options.ports[port];
        if (!current.pak_file.empty() &&
            !read_exact(current.pak_file, system.bus.controller_paks[port], "Controller Pak", error))
            return false;
        if (current.transfer.save_file.empty() && current.transfer.rtc_file.empty())
            continue;
        auto* cartridge = system.bus.transfer_paks[port].cartridge();
        if (!cartridge) {
            error = "Transfer Pak storage was configured without an inserted Game Boy cartridge on port " +
                    std::to_string(port + 1) + '.';
            return false;
        }
        if (!current.transfer.save_file.empty()) {
            if (cartridge->ram().empty()) {
                error = "A Transfer Pak RAM file was supplied for a cartridge without persistent RAM.";
                return false;
            }
            if (!read_exact(current.transfer.save_file, cartridge->ram(), "Game Boy RAM", error))
                return false;
        }
        if (!load_game_boy_rtc(current.transfer.rtc_file, *cartridge, error))
            return false;
    }
    return true;
}

bool flush_persistent_storage(const System& system, const Options& options, std::string& error) {
    error.clear();
    const auto save = cartridge_save(system);
    if (!options.save_file.empty() && !atomic_replace(options.save_file, save, "cartridge save", error))
        return false;
    if (!options.rtc_file.empty()) {
        if (!system.bus.rtc) {
            error = "Cartridge RTC hardware disappeared before storage flush.";
            return false;
        }
        if (!atomic_replace(options.rtc_file, system.bus.rtc->registers(), "cartridge RTC", error))
            return false;
    }
    for (unsigned port = 0; port < options.ports.size(); ++port) {
        const auto& current = options.ports[port];
        if (!current.pak_file.empty() &&
            !atomic_replace(current.pak_file, system.bus.controller_paks[port], "Controller Pak", error))
            return false;
        if (current.transfer.save_file.empty() && current.transfer.rtc_file.empty())
            continue;
        const auto* cartridge = system.bus.transfer_paks[port].cartridge();
        if (!cartridge) {
            error = "Transfer Pak cartridge disappeared before storage flush on port " +
                    std::to_string(port + 1) + '.';
            return false;
        }
        if (!current.transfer.save_file.empty() &&
            !atomic_replace(current.transfer.save_file, cartridge->ram(), "Game Boy RAM", error))
            return false;
        if (!current.transfer.rtc_file.empty()) {
            if (!cartridge->config().clock) {
                error = "Game Boy RTC storage is configured for a cartridge without an RTC.";
                return false;
            }
            const auto rtc = encode_game_boy_rtc(cartridge->clock());
            if (!atomic_replace(current.transfer.rtc_file, rtc, "Game Boy RTC", error))
                return false;
        }
    }
    return true;
}

} // namespace cupid::host
