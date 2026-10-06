#include "fixture.hpp"
#include <utility>

namespace test {
using namespace cupid::n64;

void instruction_tracking_tests() {
  MemoryFixture f;
  equal(f.ram.instruction_tracker() == nullptr, true);
  f.initialize();
  auto *tracker = f.ram.instruction_tracker();
  equal(tracker != nullptr, true);
  if (!tracker)
    return;
  tracker->watch(0x1ffc, 8);
  for (unsigned width : {1u, 2u, 4u, 8u}) {
    const auto first = tracker->generation(0x1000);
    const auto second = tracker->generation(0x2000);
    f.ram.write(0x1fff, width, 0xabcdef);
    equal(tracker->generation(0x1000) != first, true);
    equal(tracker->generation(0x2000), second);
  }
  const auto first = tracker->generation(0x1000);
  const auto second = tracker->generation(0x2000);
  const std::array<std::uint32_t, 2> words{1, 2};
  f.ram.write_burst(0x1ffc, words);
  equal(tracker->generation(0x1000) != first, true);
  equal(tracker->generation(0x2000) != second, true);
  const auto unchanged = tracker->generation(0x1000);
  f.ram.write(0x1000, 4, 123);
  equal(tracker->generation(0x1000), unchanged);
  f.ram.write(0x800000, 4, 1);
  equal(tracker->generation(0x1000), unchanged);
  f.ram.power(true);
  equal(tracker->generation(0x1000), unchanged);
  f.ram.write_word(0x03f00804, 0x28000000);
  equal(f.ram.instruction_tracker() == nullptr, true);
  f.ram.write_word(0x03f02804, 0x08000000);
  equal(f.ram.instruction_tracker() == tracker, true);
  equal(tracker->generation(0x1000) != unchanged, true);
  const auto mapped = tracker->generation(0x1000);
  f.ram.power();
  equal(tracker->generation(0x1000) != mapped, true);
  f.initialize();
  equal(f.ram.instruction_tracker() == tracker, true);
  const auto &readonly = std::as_const(f.ram);
  equal(readonly.words().size(), f.ram.size() / 4);
  equal(f.ram.instruction_tracker() == tracker, true);
  auto retained = f.ram.words();
  equal(f.ram.instruction_tracker() == nullptr, true);
  f.ram.power();
  f.initialize();
  retained[0x1000 / 4] = 42;
  equal(f.ram.read(0x1000, 4), 42);
  equal(f.ram.instruction_tracker() == nullptr, true);
}

} // namespace test
