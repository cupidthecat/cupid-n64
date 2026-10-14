#pragma once

#include "core/arcade/aleck64.hpp"
#include "desktop/launch/dips.hpp"
#include <filesystem>
#include <vector>

namespace cupid::desktop {

struct LaunchFiles {
  std::filesystem::path rom, firmware, ipl, disk;
  n64::ArcadeProfile arcade_profile = n64::ArcadeProfile::Disabled;
  std::vector<DipSelection> arcade_dips{};
};

struct FirmwareFiles {
  std::filesystem::path n64, arcade, ipl;
};

enum class FirmwareType { Nintendo64, Aleck64, DiskIpl };

void validate_firmware(const std::filesystem::path &path, FirmwareType type);
void validate_launch(const LaunchFiles &files);

} // namespace cupid::desktop
