#include "desktop/storage/paths.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cupid::desktop {
namespace {

template <class Paths> decltype(auto) selected_path(Paths &paths, StorageLocation location) {
  constexpr std::array members{&StoragePaths::saves, &StoragePaths::screenshots,
                               &StoragePaths::firmware, &StoragePaths::debugging};
  const auto index = static_cast<unsigned>(location);
  if (index >= members.size())
    throw std::invalid_argument("Unknown storage location.");
  return paths.*members[index];
}

} // namespace

std::filesystem::path &StoragePaths::selected(StorageLocation location) {
  return selected_path(*this, location);
}

const std::filesystem::path &StoragePaths::selected(StorageLocation location) const {
  return selected_path(*this, location);
}

std::filesystem::path StoragePaths::directory(StorageLocation location,
                                              const std::filesystem::path &image,
                                              const std::filesystem::path &application) const {
  const auto &chosen = selected(location);
  if (!chosen.empty())
    return chosen;
  return location == StorageLocation::Firmware || image.parent_path().empty() ? application
                                                                              : image.parent_path();
}

std::filesystem::path StoragePaths::save_image(const std::filesystem::path &image,
                                               std::uint64_t identity) const {
  if (saves.empty() || image.empty())
    return image;
  std::array<char, 16> hex{};
  const auto converted = std::to_chars(hex.data(), hex.data() + hex.size(), identity, 16);
  auto name = image.stem().native();
  name.push_back('-');
  name.append(hex.data(), converted.ptr);
  name += image.extension().native();
  return saves / name;
}

void validate_directory(const std::filesystem::path &directory, bool writable) {
  if (!std::filesystem::is_directory(directory))
    throw std::runtime_error("Select an existing folder.");
  const std::filesystem::directory_iterator readable(directory);
  if (!writable)
    return;
  const auto number = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto probe = directory / (".cupid-folder-check-" + std::to_string(number));
#ifdef _WIN32
  const auto file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw std::runtime_error("The selected folder does not allow creating output files.");
  CloseHandle(file);
#else
  const int file = open(probe.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
  if (file < 0)
    throw std::runtime_error("The selected folder does not allow creating output files.");
  unlink(probe.c_str());
  close(file);
#endif
}

} // namespace cupid::desktop
