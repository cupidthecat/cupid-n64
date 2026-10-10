#include "../../system/reset/fixture.hpp"
#include "input_expected.hpp"
#include "input_scenarios.hpp"

namespace test {
using namespace cupid::n64;
namespace {
struct InputProbe : reset_fixture::Machine {
  unsigned selected = 0, status = 0;
  std::array<std::uint8_t, 5> output{};
  void connect_devices() {
    console->connect_controller(0, true);
    console->connect_mouse(1);
  }
  void input(unsigned choice, unsigned buttons, int x, int y) {
    selected = choice;
    if (choice)
      console->mouse(1).input(buttons & 0x8000, buttons & 0x4000, x, y);
    else
      console->controller(0).input_host(buttons, static_cast<std::int16_t>(x),
                                        static_cast<std::int16_t>(y));
  }
  void command(unsigned choice, unsigned receive) {
    const std::array<std::uint8_t, 1> command{1};
    output.fill(0xa5);
    const auto result =
        choice ? console->mouse(1).communicate(command, std::span(output).first(receive))
               : console->controller(0).communicate(command, std::span(output).first(receive));
    status = unsigned(result.valid) | (unsigned(result.overflow) << 1);
  }
  void observe() {
    word(status);
    for (auto value : output)
      word(value);
  }
  void finish() {
    reset_fixture::Machine::finish(peripheral_input::expected, "Peripheral input");
  }
};
} // namespace
void peripheral_input_tests() {
  InputProbe probe;
  peripheral_input::scenarios(probe);
  equal(probe.block, peripheral_input::expected.size());
  equal(probe.observations, 81120);
}
} // namespace test
