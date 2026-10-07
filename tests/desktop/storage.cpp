#include "desktop/storage/save.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0, failures = 0;
void check(bool value) {
  ++checks;
  failures += !value;
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto number = std::uint64_t(now);
    do {
      path = std::filesystem::temp_directory_path() / ("cupid-saves-" + std::to_string(number++));
    } while (!std::filesystem::create_directory(path));
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
} // namespace

int main() {
  using namespace cupid::desktop;
  Directory folder;
  const auto image = folder.path / "game.n64";
  check(save_path(image, L".eep") == folder.path / "game.eep");
  check(save_path(folder.path / "game.EEP", L".eep") == folder.path / "game.eep.eep");
  check(save_path(folder.path / "game.rtc", L".rtc") == folder.path / "game.rtc.rtc");
  check(save_path(image, L".p2.pak") == folder.path / "game.p2.pak");
  for (const auto size : {512u, 2048u, 32768u, 98304u, 131072u}) {
    const auto path = folder.path / std::to_string(size);
    std::vector<std::uint8_t> memory(size);
    MemorySave original(path, memory);
    original.save();
    check(!std::filesystem::exists(path));
    for (unsigned offset = 0; offset < size; ++offset)
      memory[offset] = static_cast<std::uint8_t>(offset * 7 + 31);
    original.save();
    check(read_file(path, size) == memory);
    auto temporary = path;
    temporary += L".tmp";
    check(!std::filesystem::exists(temporary));
    const auto old_time = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(path, old_time);
    const auto saved_time = std::filesystem::last_write_time(path);
    original.save();
    check(std::filesystem::last_write_time(path) == saved_time);
    std::vector<std::uint8_t> restored(size, 255);
    MemorySave reopened(path, restored);
    check(restored == memory);
    restored.back() ^= 255;
    const auto old_contents = read_file(path, size);
    check(std::filesystem::create_directory(temporary));
    bool rejected = false;
    try {
      reopened.save();
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected);
    check(read_file(path, size) == old_contents);
    std::filesystem::remove(temporary);
    reopened.save();
    check(read_file(path, size) == restored);
    for (const auto wrong_size : {size - 1, size + 1}) {
      write_save(path, std::vector<std::uint8_t>(wrong_size, 0x89));
      rejected = false;
      memory.assign(size, 0x73);
      try {
        MemorySave invalid(path, memory);
      } catch (const std::exception &) {
        rejected = true;
      }
      check(rejected);
      check(std::all_of(memory.begin(), memory.end(), [](auto value) { return value == 0x73; }));
    }
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
