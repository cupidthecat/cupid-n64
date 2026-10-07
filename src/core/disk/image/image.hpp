#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace cupid::n64 {

class DiskImage {
public:
  bool load(std::span<const std::uint8_t> input);
  std::vector<std::uint8_t> data;
  std::vector<std::uint8_t> errors;

private:
  bool validate(std::span<const std::uint8_t> input, bool logical, bool compact);
  bool map(std::span<const std::uint8_t> input, bool logical, bool compact);
};

} // namespace cupid::n64
