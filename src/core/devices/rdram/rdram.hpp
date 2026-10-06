#pragma once

#include "core/devices/ri/ram_interface.hpp"
#include "core/timing/random.hpp"
#include <array>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <vector>

namespace cupid::n64 {

class Rdram {
public:
  Rdram(RamInterface &interface, RandomGenerator &random, bool expansion = true);
  void power(bool reset = false);
  std::uint64_t read(std::uint32_t address, unsigned bytes, bool ebus = false);
  void write(std::uint32_t address, unsigned bytes, std::uint64_t value, bool ebus = false);
  void read_burst(std::uint32_t address, std::span<std::uint32_t> words);
  void write_burst(std::uint32_t address, std::span<const std::uint32_t> words);
  std::uint32_t read_word(std::uint32_t address);
  void write_word(std::uint32_t address, std::uint32_t value, unsigned repeat_length = 0);
  std::span<std::uint32_t> words() {
    return data_;
  }
  std::span<const std::uint32_t> words() const {
    return data_;
  }
  bool identity() const {
    return identity_;
  }
  std::span<std::uint8_t> hidden() {
    return hidden_view_;
  }
  bool bind_hidden(std::span<std::uint8_t> memory);
  std::uint32_t size() const {
    return static_cast<std::uint32_t>(data_.size() * 4);
  }

private:
  struct Chip {
    bool present = false;
    bool enabled = false;
    bool auto_current = false;
    std::uint16_t device_id = 0;
    unsigned write_delay = 0;
    unsigned current = 0;
    unsigned internal_current = 0;
    unsigned low_current = 0;
    unsigned high_current = 0;
    std::array<std::uint32_t, 10> registers{};
    std::uint32_t row = 0;
  };

  static std::uint16_t decode_id(std::uint32_t value);
  static unsigned decode_current(std::uint32_t value);
  static std::uint32_t encode_current(unsigned current);
  void update_mapping();
  std::optional<std::uint32_t> translate(std::uint32_t address);
  Chip *select_chip(unsigned id);
  std::uint32_t read_register(const Chip &chip, unsigned index) const;
  void write_register(Chip &chip, unsigned index, std::uint32_t value, unsigned repeat_length);
  std::uint64_t degrade(std::uint64_t value, const Chip &chip);
  std::uint64_t read_raw(std::uint32_t address, unsigned bytes) const;
  void write_raw(std::uint32_t address, unsigned bytes, std::uint64_t value);
  void write_hidden_bit(std::uint32_t address, bool value);
  void update_hidden(std::uint32_t address, unsigned bytes, std::uint64_t value, bool ebus);
  std::uint32_t hidden_nibble(std::uint32_t address) const;

  RamInterface &interface_;
  RandomGenerator &random_;
  struct WordDeleter {
    void operator()(std::uint32_t *pointer) const {
      ::operator delete[](pointer, std::align_val_t(65536));
    }
  };
  std::unique_ptr<std::uint32_t[], WordDeleter> allocation_;
  std::span<std::uint32_t> data_;
  std::vector<std::uint8_t> hidden_;
  std::span<std::uint8_t> hidden_view_;
  std::array<Chip, 4> chips_{};
  bool identity_ = false;
};

} // namespace cupid::n64
