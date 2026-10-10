#include "fixture.hpp"
#include "core/disk/image/geometry.hpp"
#include "expected.hpp"
#include <algorithm>

namespace test::disk_boundaries {
namespace {

constexpr std::array<std::uint8_t, 6> Calendar{0x04, 0x02, 0x28, 0x23, 0x59, 0x59};
constexpr unsigned SnapshotWords = 1078;

struct Digest {
  std::uint64_t value = 14695981039346656037ull;
  unsigned words = 0;
  void add(std::uint64_t word) {
    ++words;
    for (unsigned byte = 0; byte < 8; ++byte)
      value = (value ^ ((word >> (byte * 8)) & 255)) * 1099511628211ull;
  }
};

} // namespace

Fixture::Fixture() {
  drive.connect_interrupt([this](bool line) { interrupt = line; });
  std::array<std::uint8_t, 4096> firmware;
  firmware.fill(255);
  firmware[0] = 0x80;
  firmware[1] = 0x27;
  firmware[2] = 7;
  firmware[3] = 0x40;
  std::copy_n("NDDJ", 4, firmware.begin() + 0x3b);
  equal(drive.load_ipl(firmware), true);
}

void Fixture::reset(bool present, unsigned epoch) {
  events.reset();
  events.advance(epoch, [](cupid::n64::Event) {});
  clock = epoch;
  watch = 0;
  dispatched.clear();
  if (!present)
    drive.eject();
  else if (drive.disk_data().empty()) {
    if (initial.empty()) {
      initial.resize(cupid::n64::disk_geometry::PhysicalSize);
      for (unsigned n = 0; n < initial.size(); ++n)
        initial[n] = static_cast<std::uint8_t>(n * 37u ^ (n >> 8) * 113u ^ (n >> 16) * 53u ^
                                               (n >> 24) * 29u ^ 0xa5u);
    }
    std::array<std::uint8_t, 4700> errors{};
    for (unsigned n = 0; n < errors.size(); ++n)
      errors[n] = static_cast<std::uint8_t>(n % 97 == 0);
    equal(drive.insert(initial, errors), true);
  }
  for (unsigned pair = 0; pair < 3; ++pair)
    drive.clock().write(pair, static_cast<std::uint16_t>((unsigned(Calendar[pair * 2]) << 8) |
                                                         Calendar[pair * 2 + 1]));
  drive.power();
}

void Fixture::advance(unsigned clocks) {
  clock += clocks;
  events.advance(clocks, [&](cupid::n64::Event id) {
    unsigned event = 0;
    switch (id) {
    case cupid::n64::Event::DiskClock:
      event = 1;
      break;
    case cupid::n64::Event::DiskResponse:
      event = 2;
      break;
    case cupid::n64::Event::DiskBlock:
      event = 3;
      break;
    case cupid::n64::Event::DiskMotor:
      event = 4;
      break;
    default:
      equal(false, true);
      break;
    }
    dispatched.push_back({event, clock});
    drive.event(id);
  });
}

void Fixture::command(unsigned code, unsigned parameter) {
  write(0, parameter);
  write(8, code);
}

void Fixture::write(unsigned offset, unsigned value) {
  drive.write_register(offset, static_cast<std::uint16_t>(value));
}

std::uint16_t Fixture::read(unsigned offset) {
  return drive.read_register(offset);
}

bool Fixture::select(unsigned address) {
  return drive.select(address, {});
}

unsigned Fixture::half() {
  const auto value = drive.read_half({});
  return value ? static_cast<unsigned>(*value) : 0x10000;
}

void Fixture::write_half(std::uint16_t value) {
  drive.write_half(value, {});
}

unsigned Fixture::disk_byte(unsigned address) {
  const auto disk = drive.disk_data();
  return address < disk.size() ? disk[address] : 255;
}

unsigned Fixture::transfer_offset(unsigned sector) {
  const auto track = read(12);
  return cupid::n64::disk_geometry::sector_offset(track & 4095, (track >> 12) & 1, sector,
                                                  read(40) + 1);
}

void Fixture::observe(std::string_view context) {
  Digest digest;
  digest.add(clock);
  digest.add(interrupt);
  digest.add(static_cast<std::uint32_t>(events.time_to_event()));
  digest.add(dispatched.size());
  equal(dispatched.size() <= 32, true);
  for (unsigned index = 0; index < 32; ++index) {
    digest.add(index < dispatched.size() ? dispatched[index].event : 0);
    digest.add(index < dispatched.size() ? dispatched[index].clock : 0);
  }
  // Reading status acknowledges a block interrupt and schedules its next request.
  for (unsigned offset = 0; offset < 128; offset += 2)
    digest.add(read(offset));
  digest.add(interrupt);
  digest.add(static_cast<std::uint32_t>(events.time_to_event()));
  for (unsigned pair = 0; pair < 3; ++pair)
    digest.add(drive.clock().read(pair));
  for (const auto [address, halves] :
       {std::pair{0x05000000u, 512u}, std::pair{0x05000400u, 128u}, std::pair{0x05000580u, 32u}}) {
    digest.add(select(address));
    for (unsigned word = 0; word < halves + 3; ++word)
      digest.add(half());
  }
  digest.add(watch);
  for (unsigned byte = 0; byte < 256; ++byte)
    digest.add(disk_byte(watch + byte));
  equal(digest.words, SnapshotWords);
  equal(snapshots < ExpectedState.size(), true);
  if (snapshots < ExpectedState.size()) {
    if (digest.value != ExpectedState[snapshots])
      std::cerr << "disk boundary snapshot " << snapshots << " at " << context << '\n';
    equal(digest.value, ExpectedState[snapshots]);
  }
  ++snapshots;
}

void Fixture::finish() {
  equal(snapshots, ExpectedState.size());
  equal(drive.disk_data().size(), cupid::n64::disk_geometry::PhysicalSize);
  std::uint64_t digest = 14695981039346656037ull;
  for (auto byte : drive.disk_data())
    digest = (digest ^ byte) * 1099511628211ull;
  equal(digest, ExpectedDisk);
}

} // namespace test::disk_boundaries

namespace test {

void disk_boundary_tests() {
  disk_boundaries::Fixture fixture;
  disk_boundaries::commands(fixture);
  disk_boundaries::transfers(fixture);
  disk_boundaries::simultaneous(fixture);
  fixture.finish();
}

} // namespace test
