#pragma once

#include "../../fixture.hpp"
#include "core/disk/drive.hpp"
#include <string_view>

namespace test::disk_boundaries {

struct Fixture {
  cupid::n64::EventQueue events;
  cupid::n64::DiskDrive drive{events, [] { return std::int64_t{1000000}; }};
  struct Dispatch {
    unsigned event, clock;
  };
  std::vector<Dispatch> dispatched;
  std::vector<std::uint8_t> initial;
  unsigned clock = 0, watch = 0, snapshots = 0;
  bool interrupt = false;

  Fixture();
  void reset(bool present, unsigned epoch);
  void advance(unsigned clocks);
  void command(unsigned code, unsigned parameter);
  void write(unsigned offset, unsigned value);
  std::uint16_t read(unsigned offset);
  bool select(unsigned address);
  unsigned half();
  void write_half(std::uint16_t value);
  unsigned disk_byte(unsigned address);
  unsigned transfer_offset(unsigned sector);
  void observe(std::string_view context);
  void finish();
};

void commands(Fixture &fixture);
void transfers(Fixture &fixture);
void simultaneous(Fixture &fixture);

} // namespace test::disk_boundaries
