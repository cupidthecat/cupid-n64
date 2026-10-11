#include "desktop/state/slots.hpp"
#include "../../support/test.hpp"
#include "desktop/storage/save.hpp"
#include <chrono>
#include <stdexcept>

namespace {

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

struct Directory {
  std::filesystem::path path;
  Directory() {
    auto number = std::chrono::steady_clock::now().time_since_epoch().count();
    do {
      path = std::filesystem::temp_directory_path() / ("cupid-states-" + std::to_string(number++));
    } while (!std::filesystem::create_directory(path));
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

int main() {
  using namespace cupid::desktop;
  Directory directory;
  const auto image = directory.path / "game.z64";
  std::vector<std::uint8_t> machine{1, 2, 3};
  bool capture_failed = false, restore_failed = false;
  const auto capture = [&] {
    if (capture_failed)
      throw std::runtime_error("Capture failed");
    return machine;
  };
  const auto restore = [&](std::span<const std::uint8_t> bytes) {
    if (restore_failed || bytes.empty() || bytes.front() == 255)
      throw std::runtime_error("Invalid state");
    machine.assign(bytes.begin(), bytes.end());
  };
  StateSlots states(image, capture, restore);
  test::equal(states.selected(), 1);
  test::equal(states.undo_save_slot().has_value(), false);
  test::equal(states.can_undo_load(), false);
  test::equal(states.occupied(1), false);
  test::equal(rejects([&] { states.select(0); }), true);
  test::equal(rejects([&] { states.select(11); }), true);
  test::equal(rejects([&] { states.load(); }), true);
  test::equal(rejects([&] { states.undo_save(); }), true);
  test::equal(rejects([&] { states.undo_load(); }), true);
  test::equal(states.path(1) == directory.path / "game.z64.states" / "slot-1.state", true);
  StateSlots other_image(directory.path / "game.n64", capture, restore);
  test::equal(other_image.path(1) == states.path(1), false);

  states.save();
  test::equal(states.occupied(1), true);
  test::equal(states.undo_save_slot().value_or(0), 1);
  test::equal(read_file(states.path(1), 100) == machine, true);
  states.select(10);
  states.undo_save();
  test::equal(states.occupied(1), false);
  test::equal(states.undo_save_slot().has_value(), false);
  test::equal(states.selected(), 10);
  states.save();
  machine = {4, 5, 6};
  states.save();
  test::equal(read_file(states.path(10), 100) == machine, true);

  auto temporary = states.path(10);
  temporary += L".tmp";
  std::filesystem::create_directory(temporary);
  machine = {7, 8, 9};
  test::equal(rejects([&] { states.save(); }), true);
  test::equal(read_file(states.path(10), 100) == std::vector<std::uint8_t>{4, 5, 6}, true);
  test::equal(rejects([&] { states.undo_save(); }), true);
  test::equal(states.undo_save_slot().value_or(0), 10);
  std::filesystem::remove(temporary);
  capture_failed = true;
  test::equal(rejects([&] { states.save(); }), true);
  capture_failed = false;
  states.undo_save();
  test::equal(read_file(states.path(10), 100) == std::vector<std::uint8_t>{1, 2, 3}, true);

  states.load();
  test::equal(machine == std::vector<std::uint8_t>{1, 2, 3}, true);
  test::equal(states.can_undo_load(), true);
  machine = {10, 11, 12};
  write_save(states.path(10), std::vector<std::uint8_t>{255});
  test::equal(rejects([&] { states.load(); }), true);
  test::equal(machine == std::vector<std::uint8_t>{10, 11, 12}, true);
  restore_failed = true;
  test::equal(rejects([&] { states.undo_load(); }), true);
  test::equal(states.can_undo_load(), true);
  restore_failed = false;
  states.undo_load();
  test::equal(machine == std::vector<std::uint8_t>{7, 8, 9}, true);
  test::equal(states.can_undo_load(), false);

  states.select(2);
  states.save();
  StateSlots restarted(image, capture, restore);
  restarted.select(2);
  machine = {22, 33, 44};
  restarted.load();
  test::equal(machine == std::vector<std::uint8_t>{7, 8, 9}, true);
  restarted.undo_load();
  test::equal(machine == std::vector<std::uint8_t>{22, 33, 44}, true);
  test::equal(restarted.undo_save_slot().has_value(), false);
  test::equal(std::filesystem::exists(save_path(image, L".eep")), false);
  test::equal(std::filesystem::exists(save_path(image, L".pak")), false);

  states.select(3);
  machine.clear();
  test::equal(rejects([&] { states.save(); }), true);
  test::equal(states.occupied(3), false);
  std::filesystem::create_directory(states.path(3));
  machine = {55};
  test::equal(rejects([&] { states.save(); }), true);
  test::equal(states.undo_save_slot().value_or(0), 2);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
