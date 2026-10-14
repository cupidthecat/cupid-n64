#include "desktop/launch/files.hpp"
#include "core/cartridge/profile/boot.hpp"
#include "core/cartridge/profile/profile.hpp"
#include "core/disk/drive.hpp"
#include "core/disk/image/image.hpp"
#include "desktop/storage/save.hpp"
#include <stdexcept>

namespace cupid::desktop {

void validate_firmware(const std::filesystem::path &path, FirmwareType type) {
  if (path.empty())
    throw std::runtime_error("Select the firmware file required by this launch mode.");
  if (type == FirmwareType::DiskIpl) {
    const auto bytes = read_file(path, 0x400000);
    n64::EventQueue events;
    n64::DiskDrive drive(events);
    if (!drive.load_ipl(bytes))
      throw std::runtime_error("Select a valid 64DD IPL image, from 4096 bytes through 4 MiB.");
    if (const auto boot = n64::cartridge::identify_boot(
            std::span<const std::uint8_t>(bytes).subspan(0x40, 0xfc0));
        boot && boot->model != n64::CicModel::N8303 && boot->model != n64::CicModel::N8401 &&
        boot->model != n64::CicModel::Ddus)
      throw std::runtime_error("Select 64DD IPL firmware; this file contains cartridge boot code.");
    return;
  }
  const auto bytes = read_file(path, type == FirmwareType::Nintendo64 ? 0x7c0 : 0x800);
  if (bytes.size() != 0x7c0 && (type != FirmwareType::Aleck64 || bytes.size() != 0x800))
    throw std::runtime_error(type == FirmwareType::Nintendo64
                                 ? "Nintendo 64 and 64DD require a 1984-byte PIF firmware file."
                                 : "Aleck64 requires a 1984- or 2048-byte PIF firmware file.");
}

void validate_launch(const LaunchFiles &files) {
  if (!files.arcade_dips.empty() && files.arcade_profile == n64::ArcadeProfile::Disabled)
    throw std::runtime_error("DIP settings require an arcade game.");
  const bool arcade = files.arcade_profile != n64::ArcadeProfile::Disabled;
  if (arcade && (files.rom.empty() || !files.ipl.empty() || !files.disk.empty()))
    throw std::runtime_error("Aleck64 requires a cartridge and PIF firmware, without 64DD files.");
  if (files.rom.empty() && files.ipl.empty())
    throw std::runtime_error("Select a cartridge or 64DD IPL before starting.");
  if (!files.disk.empty() && files.ipl.empty())
    throw std::runtime_error("A 64DD disk requires IPL firmware.");
  validate_firmware(files.firmware, arcade ? FirmwareType::Aleck64 : FirmwareType::Nintendo64);
  if (!files.rom.empty() && !n64::inspect_cartridge(read_file(files.rom, 0x0fc00000)))
    throw std::runtime_error("Select a valid Nintendo 64 cartridge image.");
  if (!files.ipl.empty())
    validate_firmware(files.ipl, FirmwareType::DiskIpl);
  if (!files.disk.empty()) {
    n64::DiskImage disk;
    if (!disk.load(read_file(files.disk, 0x435b0c0)))
      throw std::runtime_error("Select a valid 64DD disk image.");
  }
}

} // namespace cupid::desktop
