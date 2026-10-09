#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace cupid::n64 {

class InstructionTracker;

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
  virtual bool frozen() const {
    return false;
  }
  virtual std::span<const std::uint32_t> instruction_data(std::uint32_t) const {
    return {};
  }
  virtual std::span<const std::uint32_t> cache_fill_data(std::uint32_t) const {
    return {};
  }
  virtual bool instruction_coherent(std::uint32_t address, std::span<const std::uint32_t> words) {
    const auto data = instruction_data(address & ~4095u);
    const auto offset = (address & 4095) >> 2;
    return data.size() >= offset + words.size() &&
           std::equal(words.begin(), words.end(), data.begin() + offset);
  }
  virtual InstructionTracker *instruction_tracker() {
    return nullptr;
  }
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
