#pragma once

#include "core/devices/rdram/rdram.hpp"
#include "core/memory/bus.hpp"
#include <functional>

namespace cupid::n64 {

enum class Interrupt : unsigned { Signal, Serial, Audio, Video, Peripheral, Display };

class MipsInterface {
public:
  explicit MipsInterface(Rdram &ram);
  void connect(std::function<void(bool)> interrupt, std::function<void()> freeze = {});
  void power();
  void raise(Interrupt source);
  void lower(Interrupt source);
  std::uint32_t read_word(std::uint32_t address) const;
  void write_word(std::uint32_t address, std::uint32_t value);
  BusRead read_rdram(std::uint32_t address, unsigned bytes);
  BusWrite write_rdram(std::uint32_t address, unsigned bytes, std::uint64_t value);
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words);
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> words);
  bool frozen() const {
    return frozen_;
  }

private:
  friend class CoreState;
  void poll();
  void freeze();
  void repeat_write(std::uint32_t address, unsigned bytes, std::uint64_t value);
  Rdram &ram_;
  std::function<void(bool)> interrupt_;
  std::function<void()> freeze_;
  unsigned lines_ = 63;
  unsigned masks_ = 0;
  unsigned repeat_length_ = 0;
  bool repeat_ = false;
  bool ebus_ = false;
  bool register_select_ = false;
  bool frozen_ = false;
};

} // namespace cupid::n64
