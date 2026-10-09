#include "../fixture.hpp"
#include "core/cartridge/rtc/rtc.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr unsigned second = 187500000;

void set_running(Rtc &rtc, bool running) {
  std::array<std::uint8_t, 10> command{8, 0, 0, static_cast<std::uint8_t>(running ? 0 : 4)};
  std::array<std::uint8_t, 1> response{};
  equal(rtc.communicate(command, response).valid, true);
  equal(response[0], running ? 0 : 0x80);
}

void set_date(Rtc &rtc) {
  std::array<std::uint8_t, 10> command{8, 2, 0, 0, 0x80, 1, 0, 1, 0x25, 1};
  std::array<std::uint8_t, 1> response{};
  equal(rtc.communicate(command, response).valid, true);
}

unsigned seconds(Rtc &rtc) {
  std::array<std::uint8_t, 2> command{7, 2};
  std::array<std::uint8_t, 9> response{};
  equal(rtc.communicate(command, response).valid, true);
  equal(response[8], rtc.running() ? 0 : 0x80);
  return response[0];
}

struct TickFixture {
  EventQueue events;
  Rtc rtc{events, true, [] { return 1700000000; }};
  std::vector<Event> fired;

  explicit TickFixture(unsigned epoch = 0) {
    events.advance(epoch, [](Event) {});
    set_date(rtc);
    rtc.power();
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      fired.push_back(event);
      if (event == Event::RtcTick)
        rtc.tick();
    });
  }
  unsigned spare_capacity() const {
    auto copy = events;
    unsigned count = 0;
    while (copy.insert(Event::PeripheralRead, 1))
      ++count;
    return count;
  }
};

void canceled_deadlines() {
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (bool rearm : {false, true}) {
      TickFixture f(epoch);
      f.advance(second / 2);
      set_running(f.rtc, false);
      if (rearm)
        set_running(f.rtc, true);
      equal(f.events.time_to_event(), second / 2);
      equal(f.spare_capacity(), rearm ? 510 : 511);
      f.advance(second / 2 - 1);
      equal(f.events.time_to_event(), 1);
      f.advance(1);
      equal(f.fired.empty(), true);
      equal(seconds(f.rtc), 0);
      equal(f.events.time_to_event(), rearm ? second / 2 : 0x7fffffff);
      equal(f.spare_capacity(), rearm ? 511 : 512);
      f.advance(second / 2);
      equal(seconds(f.rtc), rearm ? 1 : 0);
      equal(f.fired.size(), rearm ? 1 : 0);
    }
}

void coincident_dma() {
  for (unsigned epoch : {0u, 0xfffffff0u})
    for (bool reverse : {false, true})
      for (bool running : {false, true}) {
        TickFixture f(epoch);
        const auto first = reverse ? Event::SerialRead : Event::PeripheralRead;
        const auto last = reverse ? Event::PeripheralRead : Event::SerialRead;
        equal(f.events.insert(first, second), true);
        equal(f.events.insert(last, second), true);
        set_running(f.rtc, running);
        equal(f.spare_capacity(), running ? 508 : 509);
        f.advance(second - 1);
        equal(f.events.time_to_event(), 1);
        equal(f.fired.empty(), true);
        f.advance(1);
        std::vector<Event> expected;
        if (running)
          expected.push_back(Event::RtcTick);
        expected.push_back(last);
        expected.push_back(first);
        equal(f.fired == expected, true);
        equal(seconds(f.rtc), running ? 1 : 0);
        equal(f.events.time_to_event(), running ? second : 0x7fffffff);
      }
}

void saturated_rearming() {
  for (unsigned rounds : {511u, 512u, 2048u}) {
    TickFixture f;
    for (unsigned n = 0; n < rounds; ++n) {
      set_running(f.rtc, false);
      set_running(f.rtc, true);
    }
    const bool ticking = rounds == 511;
    equal(f.rtc.running(), true);
    equal(f.spare_capacity(), 0);
    equal(f.events.time_to_event(), second);
    f.advance(second);
    equal(seconds(f.rtc), ticking ? 1 : 0);
    equal(f.fired.size(), ticking ? 1 : 0);
    equal(f.events.time_to_event(), ticking ? second : 0x7fffffff);
    equal(f.spare_capacity(), ticking ? 511 : 512);
    f.advance(second);
    equal(seconds(f.rtc), ticking ? 2 : 0);
    set_running(f.rtc, true);
    f.advance(second - 1);
    equal(seconds(f.rtc), ticking ? 2 : 0);
    f.advance(1);
    equal(seconds(f.rtc), ticking ? 3 : 1);
  }
}

void reset_recovery() {
  for (bool warm : {false, true})
    for (bool running : {false, true}) {
      ConsoleConfig config;
      config.rtc_present = true;
      config.rtc_clock = [] { return 1700000000; };
      auto machine = std::make_unique<Console>(config);
      auto &rtc = machine->rtc();
      set_date(rtc);
      for (unsigned n = 0; n < 512; ++n) {
        set_running(rtc, false);
        set_running(rtc, true);
      }
      machine->cpu().advance_clocks(second);
      machine->synchronize();
      equal(seconds(rtc), 0);
      if (!running)
        set_running(rtc, false);
      machine->power(warm);
      equal(machine->cpu().state().clocks, 0);
      equal(rtc.running(), running);
      machine->cpu().advance_clocks(second - 1);
      machine->synchronize();
      equal(seconds(rtc), 0);
      machine->cpu().advance_clocks(1);
      machine->synchronize();
      equal(machine->cpu().state().clocks, second);
      equal(seconds(rtc), running ? 1 : 0);
    }
}

} // namespace

void rtc_timing_tests() {
  canceled_deadlines();
  coincident_dma();
  saturated_rearming();
  reset_recovery();
}

} // namespace test
