#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

namespace cupid::n64 {

class PipelineCacheFile {
public:
  static constexpr std::size_t maximum_bytes = 64 * 1024 * 1024;
  explicit PipelineCacheFile(std::filesystem::path path) : path_(std::move(path)) {}
  bool enabled() const {
    return !path_.empty();
  }
  std::vector<std::uint8_t> read() const noexcept;
  void write(std::span<const std::uint8_t> bytes) const noexcept;

private:
  std::filesystem::path path_;
};

} // namespace cupid::n64
