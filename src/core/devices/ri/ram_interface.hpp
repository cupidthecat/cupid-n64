#pragma once

#include <array>
#include <cstdint>

namespace cupid::n64 {

class RamInterface {
public:
  void power(bool reset = false);
  std::uint32_t read_word(std::uint32_t address) const;
  void write_word(std::uint32_t address, std::uint32_t value);
  bool active() const {
    return current_loaded_ && registers_[3] == 0x14;
  }
  void acknowledge_error() {
    registers_[6] |= 1;
  }

private:
  friend class CoreState;
  std::array<std::uint32_t, 8> registers_{};
  bool current_loaded_ = false;
};

} // namespace cupid::n64
