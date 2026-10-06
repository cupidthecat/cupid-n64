#include "core/devices/cic/cic.hpp"

namespace cupid::n64 {

Cic::Cic(CicModel model) {
  power(model);
}

void Cic::power(CicModel model) {
  struct Configuration {
    unsigned seed;
    std::uint64_t checksum;
    bool pal;
    bool challenge;
    bool disk;
  };
  constexpr Configuration configurations[] = {
      {0xac, 0x93e983a8f152, false, false, false}, {0x3f, 0x45cc73ee317a, false, false, false},
      {0x3f, 0xa536c0f1d859, false, false, false}, {0x3f, 0xa536c0f1d859, true, false, false},
      {0x3f, 0x44160ec5d9af, true, false, false},  {0x78, 0x586fd4709867, false, false, false},
      {0x78, 0x586fd4709867, true, false, false},  {0x91, 0x8618a45bc2d3, false, true, false},
      {0x91, 0x8618a45bc2d3, true, true, false},   {0x85, 0x2bbad4e6eb74, false, false, false},
      {0x85, 0x2bbad4e6eb74, true, false, false},  {0xdd, 0x32b294e2ab90, false, false, true},
      {0xdd, 0x6ee8d9e84970, false, false, true},  {0xdd, 0x083c6c77e0b1, false, false, false},
      {0xde, 0x05ba2ef0a5f1, false, false, true},
  };
  const auto &configuration = configurations[static_cast<unsigned>(model)];
  seed_ = configuration.seed;
  checksum_ = configuration.checksum;
  pal_ = configuration.pal;
  real_challenge_ = configuration.challenge;
  disk_ = configuration.disk;
  state_ = State::Region;
  fifo_ = {};
  head_ = 0;
  count_ = 0;
}

void Cic::push(bool value) {
  fifo_[(head_ + static_cast<unsigned>(count_)) & 127] = value;
  ++count_;
}

bool Cic::pop() {
  const bool value = fifo_[head_];
  head_ = (head_ + 1) & 127;
  --count_;
  return value;
}

void Cic::push_nibble(unsigned value) {
  for (unsigned bit = 4; bit; --bit)
    push((value >> (bit - 1)) & 1);
}

unsigned Cic::pop_nibble() {
  unsigned value = 0;
  for (unsigned bit = 0; bit < 4; ++bit)
    value = (value << 1) | unsigned(pop());
  return value;
}

bool Cic::read_bit() {
  if (!count_)
    poll();
  return pop();
}

unsigned Cic::read_nibble() {
  if (!count_)
    poll();
  return pop_nibble();
}

void Cic::write_bit(bool value) {
  push(value);
  poll();
}

void Cic::write_nibble(unsigned value) {
  push_nibble(value);
  poll();
}

void Cic::scramble(std::span<std::uint8_t> data) {
  for (unsigned n = 1; n < data.size(); ++n)
    data[n] = (data[n] + data[n - 1] + 1) & 15;
}

void Cic::poll() {
  if (state_ == State::Region) {
    push(disk_);
    push(pal_);
    push(false);
    push(true);
    state_ = State::Seed;
  } else if (state_ == State::Seed) {
    std::array<std::uint8_t, 6> data{11,
                                     5,
                                     static_cast<std::uint8_t>(seed_ >> 4),
                                     static_cast<std::uint8_t>(seed_ & 15),
                                     static_cast<std::uint8_t>(seed_ >> 4),
                                     static_cast<std::uint8_t>(seed_ & 15)};
    scramble(data);
    scramble(data);
    for (const auto value : data)
      push_nibble(value);
    state_ = State::Checksum;
  } else if (state_ == State::Checksum) {
    std::array<std::uint8_t, 16> data{4, 7, 10, 1};
    for (unsigned n = 0; n < 12; ++n)
      data[n + 4] = (checksum_ >> (44 - n * 4)) & 15;
    for (unsigned n = 0; n < 4; ++n)
      scramble(data);
    for (const auto value : data)
      push_nibble(value);
    state_ = State::Run;
  } else if (state_ == State::Run && static_cast<unsigned>(count_) >= 2) {
    const auto upper = unsigned(pop()) << 1;
    const auto command = upper | unsigned(pop());
    if (command == 1)
      state_ = State::Dead;
    if (command == 2) {
      push_nibble(10);
      push_nibble(10);
      state_ = State::Challenge;
      begin_challenge();
    }
  } else if (state_ == State::Challenge)
    begin_challenge();
}

void Cic::begin_challenge() {
  if (count_ != 120)
    return;
  std::array<std::uint8_t, 30> data{};
  for (auto &value : data)
    value = static_cast<std::uint8_t>(pop_nibble());
  challenge(data);
  push(false);
  for (const auto value : data)
    push_nibble(value);
  state_ = State::Run;
}

void Cic::challenge(std::span<std::uint8_t, 30> data) const {
  if (!real_challenge_) {
    for (auto &value : data)
      value ^= 15;
    return;
  }
  constexpr unsigned table[] = {4, 7, 10, 7, 14, 5, 14, 1, 12, 15, 8, 15, 6, 3, 6,  9,
                                4, 1, 10, 7, 14, 5, 14, 1, 12, 9,  8, 5,  6, 3, 12, 9};
  unsigned key = 11;
  bool selector = false;
  for (auto &value : data) {
    value = (key + 5 * value) & 15;
    key = table[(unsigned(selector) << 4) | value];
    bool mode = value & 8;
    unsigned magnitude = value & 7;
    if (mode)
      magnitude ^= 7;
    if (magnitude % 3 != 1)
      mode = !mode;
    if (selector) {
      if (value == 1 || value == 9)
        mode = true;
      if (value == 11 || value == 14)
        mode = false;
    }
    selector = mode;
  }
}

} // namespace cupid::n64
