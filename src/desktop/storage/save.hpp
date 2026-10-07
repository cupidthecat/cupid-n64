#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cupid::desktop {

std::vector<std::uint8_t> read_file(const std::filesystem::path &path, std::size_t maximum);
void write_save(const std::filesystem::path &path, std::span<const std::uint8_t> data);
std::filesystem::path save_path(const std::filesystem::path &image, const wchar_t *extension);

class MemorySave {
public:
  MemorySave(std::filesystem::path path, std::span<std::uint8_t> memory);
  void save();

private:
  std::filesystem::path path_;
  std::span<std::uint8_t> memory_;
  std::vector<std::uint8_t> saved_;
};

} // namespace cupid::desktop
