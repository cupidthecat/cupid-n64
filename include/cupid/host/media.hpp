#pragma once

#include "cupid/host/options.hpp"

#include <vector>

namespace cupid::host {

enum class MediaKind { Cartridge, Pif, GameBoy };

bool read_media(const std::filesystem::path& path, MediaKind kind, std::vector<u8>& output,
                std::string& error);
[[nodiscard]] std::optional<VideoStandard> header_region(std::span<const u8> cartridge);
[[nodiscard]] std::optional<VideoStandard> firmware_region(std::span<const u8> firmware);
[[nodiscard]] std::optional<GameBoyCartridgeConfig>
game_boy_configuration(std::span<const u8> cartridge, const TransferOptions& options, std::string& error);

} // namespace cupid::host
