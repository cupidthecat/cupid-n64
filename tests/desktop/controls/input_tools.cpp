#include "../../support/test.hpp"
#include "core/controller/mouse/mouse.hpp"
#include "desktop/controls/mouse/absolute.hpp"
#include "desktop/controls/mouse/motion.hpp"
#include "desktop/controls/shortcuts/bindings.hpp"
#include <limits>

namespace {

using namespace cupid::desktop;

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

void shortcuts() {
  const auto defaults = default_shortcuts();
  validate_shortcuts(defaults);
  test::equal(shortcut_conflict(defaults).has_value(), false);
  test::equal(defaults[static_cast<unsigned>(ShortcutAction::Pause)][1].key, 0x1b);
  ShortcutBindings bindings{};
  bindings[static_cast<unsigned>(ShortcutAction::Pause)][0] = {'J', ShortcutCtrl};
  bindings[static_cast<unsigned>(ShortcutAction::FastForwardHold)] =
      std::array{ShortcutBinding{'K', ShortcutShift}, ShortcutBinding{'L', 0}};
  bindings[static_cast<unsigned>(ShortcutAction::RewindHold)][0] = {'R',
                                                                    ShortcutCtrl | ShortcutAlt};
  ShortcutRouter router;
  router.bindings(bindings);
  test::equal(router.matches('J', ShortcutCtrl), true);
  test::equal(router.matches('J', 0), false);
  test::equal(router.matches(0, 0), false);
  test::equal(router.key(0x74, 0, true).has_value(), false);
  test::equal(router.key('J', 0, true).has_value(), false);
  test::equal(router.key('J', ShortcutCtrl, true).has_value(), false);
  router.key('J', ShortcutCtrl, false);
  auto event = router.key('J', ShortcutCtrl, true);
  test::equal(event.has_value(), true);
  if (event) {
    test::equal(static_cast<unsigned>(event->action), static_cast<unsigned>(ShortcutAction::Pause));
    test::equal(event->pressed, true);
  }
  for (unsigned repeat = 0; repeat < 10; ++repeat)
    test::equal(router.key('J', ShortcutCtrl, true).has_value(), false);
  test::equal(router.key('J', 0, false).has_value(), false);
  test::equal(router.key('J', ShortcutCtrl, true).has_value(), true);
  event = router.key('K', ShortcutShift, true);
  test::equal(event.has_value(), true);
  test::equal(router.key('L', 0, true).has_value(), false);
  test::equal(router.key('K', 0, false).has_value(), false);
  event = router.key('L', ShortcutCtrl, false);
  test::equal(event.has_value(), true);
  if (event)
    test::equal(event->pressed, false);
  event = router.key('R', ShortcutCtrl | ShortcutAlt, true);
  test::equal(event.has_value(), true);
  test::equal(router.key(0x11, ShortcutAlt, false).has_value(), false);
  event = router.key('R', 0, false);
  test::equal(event.has_value(), true);
  if (event)
    test::equal(event->pressed, false);
  router.key('K', ShortcutShift, true);
  router.clear();
  test::equal(router.key('K', 0, false).has_value(), false);
  test::equal(router.key(256, 0, true).has_value(), false);
  const auto old = bindings;
  bindings[static_cast<unsigned>(ShortcutAction::Mute)][0] = {'J', ShortcutCtrl};
  const auto conflict = shortcut_conflict(bindings);
  test::equal(conflict.has_value(), true);
  if (conflict)
    test::equal(conflict->binding.key, 'J');
  test::equal(rejects([&] { router.bindings(bindings); }), true);
  test::equal(router.matches('J', ShortcutCtrl), true);
  bindings = old;
  bindings[0][0] = {0, ShortcutCtrl};
  test::equal(rejects([&] { validate_shortcuts(bindings); }), true);
  test::equal(valid_shortcut({0x10, 0}), false);
  test::equal(valid_shortcut({'Q', 8}), false);
  test::equal(valid_shortcut({255, ShortcutAlt}), true);
  router.bindings(defaults);
  test::equal(router.matches(0x74, 0), true);
  router.bindings({});
  test::equal(router.matches(0x74, 0), false);
}

std::uint32_t report(cupid::n64::Mouse &mouse) {
  const std::array<std::uint8_t, 1> command{1};
  std::array<std::uint8_t, 4> bytes{};
  test::equal(mouse.communicate(command, bytes).valid, true);
  return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
         (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

void mouse() {
  MouseMotion motion;
  cupid::n64::Mouse mouse;
  unsigned reads = 0;
  mouse.input_reader([&] {
    ++reads;
    const auto input = motion.read();
    return cupid::n64::Mouse::Input{input.mouse_left, input.mouse_right, input.mouse_x,
                                    input.mouse_y};
  });
  const std::array<std::uint8_t, 1> identify{0}, invalid{2};
  std::array<std::uint8_t, 4> bytes{};
  mouse.communicate(identify, bytes);
  mouse.communicate(invalid, bytes);
  test::equal(reads, 0);
  motion.input(true, false, 5, 3);
  motion.input(true, false, 2, -5);
  test::equal(report(mouse), 0x80000702);
  test::equal(report(mouse), 0x80000000);
  motion.input(false, true, 500, -500);
  int x = 0, y = 0;
  for (unsigned poll = 0; poll < 4; ++poll) {
    const auto value = report(mouse);
    test::equal(value >> 30, 1);
    x += static_cast<std::int8_t>(value >> 8);
    y += static_cast<std::int8_t>(value);
  }
  test::equal(x, 500);
  test::equal(y, 500);
  test::equal(report(mouse), 0x40000000);
  motion.input(false, false, -128, 128);
  test::equal(report(mouse), 0x00008080);
  test::equal(report(mouse), 0);
  motion.input(true, true, 300, -300);
  motion.clear();
  test::equal(report(mouse), 0);
  motion.input(true, false, 300, -300);
  auto input = motion.take();
  test::equal(input.mouse_x, 300);
  test::equal(input.mouse_y == -300, true);
  test::equal(motion.take().mouse_x, 0);
  motion.input(false, false, std::numeric_limits<std::int32_t>::max(), 0);
  motion.input(false, false, 1, 0);
  test::equal(motion.take().mouse_x, std::numeric_limits<std::int32_t>::max());
  motion.input(false, false, std::numeric_limits<std::int32_t>::min(), 0);
  motion.input(false, false, -1, 0);
  test::equal(motion.take().mouse_x == std::numeric_limits<std::int32_t>::min(), true);
  mouse.input_reader({});
  mouse.input(false, true, 9, -2);
  test::equal(report(mouse), 0x40000902);
  test::equal(report(mouse), 0x40000902);
}

void absolute_mouse() {
  AbsoluteMouseMotion motion;
  test::equal(motion.input(false, 0, 0, 1000, 500).mouse_x, 0);
  auto delta = motion.input(false, 32767, 32767, 1000, 500);
  test::equal(delta.mouse_x, 500);
  test::equal(delta.mouse_y, 250);
  test::equal(motion.input(true, 32767, 32767, 1200, 800).mouse_x, 0);
  delta = motion.input(false, 65535, 0, 1000, 500);
  test::equal(delta.mouse_x, 500);
  test::equal(delta.mouse_y == -250, true);
  delta = motion.input(true, 65535, 65535, 1200, 800);
  test::equal(delta.mouse_x, 600);
  test::equal(delta.mouse_y, 400);
  test::equal(motion.input(false, 65535, 65535, 2000, 1000).mouse_x, 0);
  delta = motion.input(false, 0, 0, 2000, 1000);
  test::equal(delta.mouse_x == -2000, true);
  test::equal(delta.mouse_y == -1000, true);
  motion.clear();
  test::equal(motion.input(false, 65535, 65535, 1000, 500).mouse_x, 0);
  test::equal(motion.input(false, 0, 0, 0, 500).mouse_x, 0);
  test::equal(motion.input(false, 0, 0, 1000, 500).mouse_x, 0);
}

} // namespace

int main() {
  shortcuts();
  mouse();
  absolute_mouse();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures != 0;
}
