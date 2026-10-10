#include "../../system/reset/fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"
namespace test {
using namespace cupid::n64;
namespace {
struct BioProbe : reset_fixture::Machine {
  std::uint64_t now = 0;
  unsigned response_status = 0;
  std::array<std::uint8_t, 63> response{};
  void clock(std::uint64_t value) {
    now = value;
  }
  void connect_sensor(unsigned bpm) {
    console->connect_controller(0, true);
    auto &pad = console->controller(0);
    pad.bio_sensor([this] { return now; });
    pad.sensor().beats_per_minute(bpm);
  }
  void disconnect() {
    console->controller(0).disconnect_pak();
  }
  unsigned address_crc(unsigned address) {
    return cupid::n64::address_crc(address);
  }
  void command(std::span<const std::uint8_t> data, unsigned receive) {
    response.fill(0xa5);
    const auto status =
        console->controller(0).communicate(data, std::span(response).first(receive));
    response_status = unsigned(status.valid) | (unsigned(status.overflow) << 1);
  }
  void observe() {
    word(response_status);
    for (auto value : response)
      word(value);
    word(console->controller(0).rumbling());
  }
  void finish() {
    reset_fixture::Machine::finish(bio_clock::expected, "Bio Sensor clock");
  }
};
} // namespace
void bio_clock_tests() {
  BioProbe probe;
  bio_clock::scenarios(probe);
  equal(probe.block, bio_clock::expected.size());
  equal(probe.observations, 409500);
}
} // namespace test
