#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace cupid::n64 {

enum class CicModel {
  N5101,
  N6101,
  N6102,
  N7101,
  N7102,
  N6103,
  N7103,
  N6105,
  N7105,
  N6106,
  N7106,
  N8303,
  N8401,
  N5167,
  Ddus,
};

class Cic {
public:
  explicit Cic(CicModel model = CicModel::N6102);
  void power(CicModel model);
  bool read_bit();
  unsigned read_nibble();
  void write_bit(bool value);
  void write_nibble(unsigned value);

private:
  enum class State { Region, Seed, Checksum, Run, Challenge, Dead };
  void poll();
  void push(bool value);
  bool pop();
  void push_nibble(unsigned value);
  unsigned pop_nibble();
  static void scramble(std::span<std::uint8_t> data);
  void begin_challenge();
  void challenge(std::span<std::uint8_t, 30> data) const;
  std::array<bool, 128> fifo_{};
  unsigned head_ = 0;
  int count_ = 0;
  unsigned seed_ = 0;
  std::uint64_t checksum_ = 0;
  bool pal_ = false;
  bool disk_ = false;
  bool real_challenge_ = false;
  State state_ = State::Region;
};

} // namespace cupid::n64
