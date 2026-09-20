#pragma once

#include "cupid/system.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace cupid::host {

struct TransferOptions {
    std::filesystem::path cartridge;
    std::filesystem::path save_file;
    std::filesystem::path rtc_file;
    std::optional<GameBoyMapper> mapper;
    std::optional<unsigned> ram_bytes;
    std::optional<bool> clock;
    std::optional<bool> rumble;
};

struct PortOptions {
    ControllerState controller{false, 0, 0, 0, ControllerAccessory::None, ControllerDevice::Gamepad};
    bool controller_selected{};
    bool accessory_selected{};
    std::filesystem::path pak_file;
    TransferOptions transfer;
};

struct Options {
    std::filesystem::path cartridge;
    std::filesystem::path pif;
    std::filesystem::path save_file;
    std::filesystem::path rtc_file;
    u64 max_instructions{4000000000ULL};
    bool require_success{};
    bool require_extended{};
    bool help{};
    std::optional<VideoStandard> region;
    std::optional<VideoStandard> pif_region;
    unsigned ram_mib{8};
    std::optional<SaveType> save;
    unsigned sram_bytes{32U * 1024U};
    std::optional<FlashChip> flash_chip;
    std::optional<CicModel> cic;
    bool rtc{};
    std::array<PortOptions, 4> ports;
};

[[nodiscard]] std::optional<Options> parse_options(std::span<const std::string_view> arguments,
                                                   std::string& error);
[[nodiscard]] std::string_view usage();
[[nodiscard]] std::filesystem::path argument_path(std::string_view text);
[[nodiscard]] std::string path_text(const std::filesystem::path& path);

} // namespace cupid::host
