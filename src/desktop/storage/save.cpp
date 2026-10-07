#include "desktop/storage/save.hpp"
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cupid::desktop {

std::vector<std::uint8_t> read_file(const std::filesystem::path &path, std::size_t maximum) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream)
    throw std::runtime_error("Could not open the selected file.");
  const auto length = stream.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > maximum)
    throw std::runtime_error("The selected file has an invalid size.");
  std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char *>(data.data()), length))
    throw std::runtime_error("Could not read the selected file.");
  return data;
}

void write_save(const std::filesystem::path &path, std::span<const std::uint8_t> data) {
  auto temporary = path;
  temporary += L".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
  stream.close();
  if (!stream)
    throw std::runtime_error("Could not write a save file. Check folder permissions.");
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Could not replace the save file.");
#else
  std::filesystem::rename(temporary, path);
#endif
}

std::filesystem::path save_path(const std::filesystem::path &image, const wchar_t *extension) {
  auto path = image;
  path.replace_extension(extension);
  auto original_name = image.filename().wstring();
  auto save_name = path.filename().wstring();
  const auto lower = [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); };
  std::transform(original_name.begin(), original_name.end(), original_name.begin(), lower);
  std::transform(save_name.begin(), save_name.end(), save_name.begin(), lower);
  if (original_name == save_name)
    path += extension;
  return path;
}

MemorySave::MemorySave(std::filesystem::path path, std::span<std::uint8_t> memory)
    : path_(std::move(path)), memory_(memory) {
  if (std::filesystem::exists(path_)) {
    const auto data = read_file(path_, memory_.size());
    if (data.size() != memory_.size())
      throw std::runtime_error(
          "The save file has an invalid size for this cartridge or accessory.");
    std::copy(data.begin(), data.end(), memory_.begin());
  }
  saved_.assign(memory_.begin(), memory_.end());
}

void MemorySave::save() {
  if (!std::equal(memory_.begin(), memory_.end(), saved_.begin())) {
    write_save(path_, memory_);
    saved_.assign(memory_.begin(), memory_.end());
  }
}

} // namespace cupid::desktop
