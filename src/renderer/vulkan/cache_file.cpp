#include "renderer/vulkan/cache_file.hpp"
#include <atomic>
#include <fstream>
#include <string>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cupid::n64 {

std::vector<std::uint8_t> PipelineCacheFile::read() const noexcept {
  if (!enabled())
    return {};
  try {
    std::ifstream input(path_, std::ios::binary | std::ios::ate);
    const std::streamoff size = input ? input.tellg() : std::streampos(0);
    if (size <= 0 || size > static_cast<std::streamoff>(maximum_bytes))
      return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return input ? std::move(bytes) : std::vector<std::uint8_t>{};
  } catch (...) {
    return {};
  }
}

void PipelineCacheFile::write(std::span<const std::uint8_t> bytes) const noexcept {
  if (!enabled() || bytes.empty() || bytes.size() > maximum_bytes)
    return;
  std::filesystem::path temporary;
  try {
    std::error_code error;
    if (!path_.parent_path().empty()) {
      std::filesystem::create_directories(path_.parent_path(), error);
      if (error)
        return;
    }
    static std::atomic<unsigned> sequence = 0;
#if defined(_WIN32)
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    temporary = path_;
    temporary += ".tmp-" + std::to_string(process) + "-" + std::to_string(sequence++);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (output) {
      output.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
      output.close();
    }
    if (output) {
#if defined(_WIN32)
      MoveFileExW(temporary.c_str(), path_.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
      std::filesystem::rename(temporary, path_, error);
#endif
    }
  } catch (...) {
  }
  if (!temporary.empty()) {
    std::error_code error;
    std::filesystem::remove(temporary, error);
  }
}

} // namespace cupid::n64
