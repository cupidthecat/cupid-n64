#include "core/disk/clock/clock.hpp"
#include "../fixture.hpp"

namespace test {
using namespace cupid::n64;

void disk_clock_tests() {
  std::int64_t now = 1000000;
  DiskClock clock([&] { return now; });
  std::array<std::uint8_t, 16> saved{0x99, 0x12, 0x31, 0x23, 0x59, 0x59, 255, 255};
  for (unsigned byte = 0; byte < 8; ++byte)
    saved[8 + byte] = static_cast<std::uint8_t>(std::uint64_t(now) >> (byte * 8));
  equal(clock.load(saved), true);
  equal(clock.valid(), true);
  clock.tick();
  equal(clock.read(0), 1);
  equal(clock.read(1), 0x100);
  equal(clock.read(2), 0);
  equal(clock.read(3), 0xffff);
  clock.write(0, 0x0002);
  clock.write(1, 0x2823);
  clock.write(2, 0x5959);
  clock.tick();
  equal(clock.read(1), 0x2900);
  clock.write(1, 0x2923);
  clock.write(2, 0x5959);
  clock.tick();
  equal(clock.read(0), 3);
  equal(clock.read(1), 0x100);
  clock.write(0, 0x0102);
  clock.write(1, 0x2900);
  equal(clock.valid(), false);
  clock.tick();
  equal(clock.read(1), 0x2900);
  equal(clock.load(clock.save()), true);
  equal(clock.read(0), 0xffff);
  equal(clock.read(3), 0xffff);
  now += 86401;
  equal(clock.load(saved), true);
  equal(clock.read(0), 1);
  equal(clock.read(1), 0x200);
  equal(clock.read(2), 0);
  equal(clock.load(std::span(saved).first(15)), false);
  const auto before = clock.save();
  now += 36525ll * 86400;
  equal(clock.load(before), true);
  equal(clock.read(0), 1);
  equal(clock.read(1), 0x200);
}

} // namespace test
