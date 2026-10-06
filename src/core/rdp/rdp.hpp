#pragma once

#include "core/rsp/rsp.hpp"
#include <functional>

namespace cupid::n64 {

struct DisplayCommandState {
  std::uint32_t start = 0, end = 0, current = 0, clock = 0;
  std::uint32_t buffer_busy = 0, pipe_busy = 0, tmem_busy = 0;
  bool source = false, freeze = false, crashed = false, flush = false;
  bool start_valid = false, end_valid = false, start_clock = false, ready = true;
};

class Rdp {
public:
  Rdp(Rdram &ram, Rsp &rsp, MipsInterface &interrupts);
  void power();
  void advance(std::uint32_t clocks);
  void elapse(std::uint32_t clocks) {
    clock_ -= clocks;
  }
  void run();
  void connect(std::function<void(std::span<const std::uint32_t>)> submit,
               std::function<void()> synchronize);
  void connect_sync(std::function<void()> callback);
  std::uint32_t read_word(std::uint32_t address, std::int64_t caller_clock = 0,
                          bool cpu = true) const;
  void write_word(std::uint32_t address, std::uint32_t value, std::int64_t caller_clock = 0,
                  bool cpu = true);
  std::uint32_t read_test(std::uint32_t address) const;
  void write_test(std::uint32_t address, std::uint32_t value);
  void flush_commands();
  void sync_full();
  void crash();
  const DisplayCommandState &command() const {
    return command_;
  }
  std::int64_t clocks() const {
    return clock_;
  }
  unsigned buffered_words() const {
    return queue_size_;
  }
  unsigned consumed_words() const {
    return queue_offset_;
  }

private:
  void render();
  Rdram &ram_;
  Rsp &rsp_;
  MipsInterface &interrupts_;
  DisplayCommandState command_{};
  std::int64_t clock_ = 0;
  std::array<std::uint32_t, 65536> buffer_{};
  unsigned queue_size_ = 0, queue_offset_ = 0;
  struct TestState {
    bool check = false, go = false, done = false, enable = false;
    std::uint8_t fail = 0, address = 0;
    std::array<std::uint32_t, 128> data{};
  } test_;
  std::function<void(std::span<const std::uint32_t>)> submit_;
  std::function<void()> synchronize_;
  std::function<void()> sync_;
};

} // namespace cupid::n64
