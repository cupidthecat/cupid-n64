#include "desktop/state/rewind.hpp"
#include "../../support/test.hpp"
#include <stdexcept>

namespace {

using cupid::desktop::RewindHistory;

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

void history_and_forks() {
  std::uint8_t machine = 1;
  unsigned captures = 0;
  bool capture_failed = false, restore_failed = false;
  RewindHistory history(
      [&] {
        ++captures;
        if (capture_failed)
          throw std::runtime_error("Capture failed");
        return std::vector<std::uint8_t>{machine};
      },
      [&](std::span<const std::uint8_t> bytes) {
        if (restore_failed)
          throw std::runtime_error("Restore failed");
        machine = bytes.front();
      },
      {10, 3, 8});
  test::equal(history.record(10), true);
  machine = 2;
  test::equal(history.record(19), false);
  test::equal(captures, 1);
  test::equal(history.record(20), true);
  machine = 3;
  test::equal(history.record(30), true);
  test::equal(history.size(), 3);
  capture_failed = true;
  test::equal(rejects([&] { history.record(40); }), true);
  test::equal(history.size(), 3);
  capture_failed = false;
  restore_failed = true;
  test::equal(rejects([&] { history.rewind(30); }), true);
  test::equal(history.size(), 3);
  test::equal(machine, 3);
  restore_failed = false;
  test::equal(history.rewind(30), true);
  test::equal(machine, 2);
  test::equal(history.size(), 2);
  test::equal(history.rewind(20), true);
  test::equal(machine, 1);
  test::equal(history.rewind(10), false);
  test::equal(history.can_rewind(11), true);
  machine = 8;
  test::equal(history.record(20), true);
  test::equal(history.size(), 2);
  machine = 9;
  test::equal(history.record(30), true);
  machine = 10;
  test::equal(history.record(40), true);
  test::equal(history.size(), 3);
  test::equal(history.rewind(40), true);
  test::equal(machine, 9);
  test::equal(history.rewind(30), true);
  test::equal(machine, 8);
  test::equal(history.can_rewind(20), false);
  history.configure({10, 3, 8});
  test::equal(history.size(), 1);
  test::equal(rejects([&] { history.configure({0, 3, 8}); }), true);
  test::equal(history.size(), 1);
  history.configure({20, 3, 8});
  test::equal(history.size(), 0);
  test::equal(history.memory(), 0);
  test::equal(history.record(100), true);
  machine = 12;
  test::equal(history.record(1), true);
  test::equal(history.size(), 1);
  test::equal(history.rewind(2), true);
  test::equal(machine, 12);
}

void memory_limits() {
  std::size_t allocation = 2;
  RewindHistory history(
      [&] {
        std::vector<std::uint8_t> bytes(1, 1);
        bytes.reserve(allocation);
        return bytes;
      },
      [](std::span<const std::uint8_t>) {}, {1, 10, 5});
  history.record(1);
  history.record(2);
  history.record(3);
  test::equal(history.size(), 2);
  test::equal(history.memory() <= 5, true);
  test::equal(history.can_rewind(2), false);
  allocation = 6;
  test::equal(rejects([&] { history.record(4); }), true);
  test::equal(history.size(), 2);
  test::equal(history.memory() <= 5, true);
  history.clear();
  test::equal(history.size(), 0);
  test::equal(history.memory(), 0);
}

} // namespace

int main() {
  history_and_forks();
  memory_limits();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
