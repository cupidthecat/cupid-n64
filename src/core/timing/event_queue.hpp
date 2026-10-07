#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>

namespace cupid::n64 {

enum class Event : unsigned {
  PeripheralRead,
  PeripheralWrite,
  PeripheralBusWrite,
  SerialRead,
  SerialWrite,
  SerialBusWrite,
  ClockTick,
  EepromWrite,
  FlashComplete,
  RtcTick,
};

class EventQueue {
public:
  void connect_insert(std::function<void()> callback) {
    inserted_ = std::move(callback);
  }
  void reset() {
    clock_ = size_ = 0;
  }

  bool insert(Event event, std::uint32_t clocks) {
    if (size_ == heap_.size())
      return false;
    unsigned child = size_++;
    clocks += clock_;
    while (child) {
      const auto parent = (child - 1) >> 1;
      if (at_or_after(clocks, heap_[parent].clock))
        break;
      heap_[child] = heap_[parent];
      child = parent;
    }
    heap_[child] = {clocks, event, true};
    if (inserted_)
      inserted_();
    return true;
  }

  std::uint32_t cancel(Event event) {
    std::uint32_t remaining = 0;
    for (unsigned n = 0; n < size_; ++n) {
      if (heap_[n].event == event) {
        heap_[n].valid = false;
        const auto clocks = heap_[n].clock - clock_;
        if (clocks > remaining)
          remaining = clocks;
      }
    }
    return remaining;
  }

  void remove(Event event) {
    unsigned count = 0;
    for (unsigned n = 0; n < size_; ++n)
      if (heap_[n].event != event)
        heap_[count++] = heap_[n];
    size_ = count;
    for (unsigned index = size_ / 2; index > 0;) {
      unsigned parent = --index;
      const auto entry = heap_[parent];
      while (true) {
        unsigned child = parent * 2 + 1;
        if (child >= size_)
          break;
        if (child + 1 < size_ && at_or_after(heap_[child].clock, heap_[child + 1].clock))
          ++child;
        if (at_or_after(heap_[child].clock, entry.clock))
          break;
        heap_[parent] = heap_[child];
        parent = child;
      }
      heap_[parent] = entry;
    }
  }

  std::int32_t time_to_event() const {
    return size_ ? std::bit_cast<std::int32_t>(heap_[0].clock - clock_) : 0x7fffffff;
  }

  template <typename Callback> void advance(std::uint32_t clocks, Callback callback) {
    clock_ += clocks;
    while (size_ && at_or_after(clock_, heap_[0].clock)) {
      if (const auto event = pop())
        callback(*event);
    }
  }

private:
  struct Entry {
    std::uint32_t clock = 0;
    Event event{};
    bool valid = false;
  };
  std::function<void()> inserted_;

  static bool at_or_after(std::uint32_t a, std::uint32_t b) {
    return a - b < 0x7fffffff;
  }

  std::optional<Event> pop() {
    const auto root = heap_[0];
    const auto last = heap_[--size_];
    unsigned parent = 0;
    while (true) {
      unsigned child = parent * 2 + 1;
      if (child >= size_)
        break;
      if (child + 1 < size_ && at_or_after(heap_[child].clock, heap_[child + 1].clock))
        ++child;
      if (at_or_after(heap_[child].clock, last.clock))
        break;
      heap_[parent] = heap_[child];
      parent = child;
    }
    heap_[parent] = last;
    return root.valid ? std::optional(root.event) : std::nullopt;
  }

  std::array<Entry, 512> heap_{};
  std::uint32_t clock_ = 0;
  unsigned size_ = 0;
};

} // namespace cupid::n64
