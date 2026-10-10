#include "../../system/reset/fixture.hpp"
#include "wire_expected.hpp"
#include "wire_scenarios.hpp"

namespace test {
using namespace cupid::n64;
namespace {
struct WireProbe : reset_fixture::Machine {
  unsigned device = 0;
  unsigned response_status = 0;
  std::array<std::uint8_t, 63> response{};
  bool connected = false;
  void connect(unsigned choice) {
    device = choice;
    connected = true;
    if (choice == 4) {
      console->connect_mouse(0);
      console->mouse(0).input(false, false, 0, 0);
    } else {
      console->connect_controller(0, true);
      auto &pad = console->controller(0);
      pad.input(0, 0, 0);
      if (choice == 1) {
        pad.memory_pak(1);
        for (unsigned n = 0; n < pad.pak_data().size(); ++n)
          pad.pak_data()[n] = peripheral_wire::pattern(n);
      }
      if (choice == 2)
        pad.rumble_pak();
      if (choice == 3)
        pad.transfer_pak();
    }
  }
  void disconnect() {
    if (device == 4) {
      console->connect_mouse(0, false);
      connected = false;
    } else {
      console->controller(0).disconnect_pak();
    }
  }
  unsigned address_crc(unsigned address) {
    return cupid::n64::address_crc(address);
  }
  void command(std::span<const std::uint8_t> data, unsigned receive) {
    response.fill(0xa5);
    const auto status =
        !connected ? cupid::n64::JoybusStatus{}
        : device == 4
            ? console->mouse(0).communicate(data, std::span(response).first(receive))
            : console->controller(0).communicate(data, std::span(response).first(receive));
    response_status = unsigned(status.valid) | (unsigned(status.overflow) << 1);
  }
  void observe() {
    word(response_status);
    for (auto value : response)
      word(value);
    word(device != 4 && console->controller(0).rumbling());
  }
  void finish() {
    reset_fixture::Machine::finish(peripheral_wire::expected, "Peripheral commands");
  }
};
} // namespace
void peripheral_wire_tests() {
  WireProbe probe;
  peripheral_wire::scenarios(probe);
  equal(probe.block, peripheral_wire::expected.size());
  equal(probe.observations, 975000);
}
} // namespace test
