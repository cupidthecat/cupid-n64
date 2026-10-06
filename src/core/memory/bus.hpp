#pragma once

#include <cstdint>
#include <span>

namespace cupid::n64 {

struct BusRead {
  std::uint64_t value = 0;
  std::uint32_t clocks = 0;
  bool success = true;
};

struct BusWrite {
  std::uint32_t clocks = 0;
  bool success = true;
};

class Bus {
public:
  virtual ~Bus() = default;
  virtual BusRead read(std::uint32_t address, unsigned bytes) = 0;
  virtual BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) = 0;

  virtual BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words) {
    BusWrite result;
    for (auto &word : words) {
      const auto transfer = read(address, 4);
      result.clocks += transfer.clocks;
      result.success &= transfer.success;
      word = static_cast<std::uint32_t>(transfer.value);
      address += 4;
    }
    return result;
  }

  virtual BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> words) {
    BusWrite result;
    for (auto word : words) {
      const auto transfer = write(address, 4, word);
      result.clocks += transfer.clocks;
      result.success &= transfer.success;
      address += 4;
    }
    return result;
  }
};

} // namespace cupid::n64
