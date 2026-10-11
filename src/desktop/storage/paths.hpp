#pragma once

#include <cstdint>
#include <filesystem>

namespace cupid::desktop {

enum class StorageLocation { Saves, Screenshots, Firmware, Debugging };

struct StoragePaths {
  std::filesystem::path saves, screenshots, firmware, debugging;
  std::filesystem::path &selected(StorageLocation location);
  const std::filesystem::path &selected(StorageLocation location) const;
  std::filesystem::path directory(StorageLocation location, const std::filesystem::path &image,
                                  const std::filesystem::path &application) const;
  std::filesystem::path save_image(const std::filesystem::path &image,
                                   std::uint64_t identity) const;
};

void validate_directory(const std::filesystem::path &directory, bool writable);

} // namespace cupid::desktop
