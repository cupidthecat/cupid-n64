#include "../fixture.hpp"
#include "core/cartridge/joybus.hpp"
#include "core/system/console.hpp"
#include <algorithm>
#include <ctime>

namespace test {
using namespace cupid::n64;
namespace {

struct ClockFixture {
  EventQueue events;
  std::int64_t now = 1700000000;
  Rtc rtc{events, true, [this] { return now; }};
  ClockFixture() {
    rtc.power();
  }
  std::array<std::uint8_t, 9> read(unsigned block) {
    std::array<std::uint8_t, 2> command{7, static_cast<std::uint8_t>(block)};
    std::array<std::uint8_t, 9> response{};
    equal(rtc.communicate(command, response).valid, true);
    return response;
  }
  void write(unsigned block, std::array<std::uint8_t, 8> data) {
    std::array<std::uint8_t, 10> command{8, static_cast<std::uint8_t>(block)};
    std::copy(data.begin(), data.end(), command.begin() + 2);
    std::array<std::uint8_t, 1> response{};
    equal(rtc.communicate(command, response).valid, true);
    equal(response[0], rtc.running() ? 0 : 0x80);
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) {
      equal(static_cast<unsigned>(event), static_cast<unsigned>(Event::RtcTick));
      rtc.tick();
    });
  }
};

void packet_lengths() {
  EventQueue events;
  Rtc absent(events);
  std::array<std::uint8_t, 12> command{}, response{};
  for (unsigned operation = 6; operation <= 8; ++operation) {
    command[0] = static_cast<std::uint8_t>(operation);
    equal(absent.communicate(command, response).valid, false);
    for (unsigned send = 0; send <= command.size(); ++send)
      for (unsigned receive = 0; receive <= response.size(); ++receive) {
        ClockFixture fixture;
        command.fill(0);
        command[0] = static_cast<std::uint8_t>(operation);
        response.fill(0xa5);
        const auto result = fixture.rtc.communicate(std::span(command).first(send),
                                                    std::span(response).first(receive));
        const bool valid = operation == 6   ? send >= 1 && receive >= 3
                           : operation == 7 ? send >= 2 && receive >= 9
                                            : send >= 10 && receive >= 1;
        equal(result.valid, valid);
        equal(result.overflow, false);
        const auto written = valid ? (operation == 6 ? 3u : operation == 7 ? 9u : 1u) : 0u;
        for (unsigned n = written; n < response.size(); ++n)
          equal(response[n], 0xa5);
      }
  }
  equal(absent.present(), false);
  equal(events.time_to_event(), 0x7fffffff);
}

void calendar() {
  ClockFixture f;
  f.write(2, {0x59, 0x59, 0xa3, 0x28, 0x06, 0x02, 0x24, 0x01});
  f.advance(187499999);
  equal(f.read(2)[0], 0x59);
  f.advance(1);
  auto date = f.read(2);
  constexpr std::array<std::uint8_t, 8> leap{0, 0, 0x80, 0x29, 0, 2, 0x24, 1};
  for (unsigned n = 0; n < 8; ++n)
    equal(date[n], leap[n]);
  equal(f.events.time_to_event(), 187500000);
  constexpr std::array<unsigned, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const auto bcd = [](unsigned n) { return static_cast<std::uint8_t>((n / 10 << 4) | (n % 10)); };
  for (unsigned month = 1; month <= 12; ++month) {
    f.write(2, {0x59, 0x59, 0xa3, bcd(days[month - 1]), 0x03, bcd(month), 0x25, 0x01});
    f.rtc.advance(1);
    date = f.read(2);
    equal(date[0], 0);
    equal(date[1], 0);
    equal(date[2], 0x80);
    equal(date[3], 1);
    equal(date[4], 4);
    equal(date[5], bcd(month == 12 ? 1 : month + 1));
    equal(date[6], month == 12 ? 0x26 : 0x25);
    equal(date[7], 1);
  }
  f.write(2, {0x59, 0x59, 0xa3, 0x31, 0x06, 0x12, 0x99, 0});
  f.rtc.advance(1);
  date = f.read(2);
  equal(date[3], 1);
  equal(date[5], 1);
  equal(date[6], 0);
  equal(date[7], 1);
  f.write(2, {0x59, 0x59, 0xa3, 0x28, 0, 2, 0, 2});
  f.rtc.advance(1);
  equal(f.read(2)[3], 0x29);
  f.write(2, {0x99, 0, 0x80, 1, 0, 1, 0x25, 1});
  f.rtc.advance(162);
  date = f.read(2);
  equal(date[0], 5);
  equal(date[1], 0);
}

void control_and_locks() {
  ClockFixture f;
  f.write(1, {1, 2, 3, 4, 5, 6, 7, 8});
  f.write(2, {0, 0, 0x80, 1, 0, 1, 0x25, 1});
  const auto before = f.read(2);
  f.write(0, {3, 4, 0, 0, 0, 0, 0, 0});
  equal(f.rtc.running(), false);
  equal(f.events.time_to_event(), 187500000);
  f.write(1, {});
  f.write(2, {});
  f.advance(187500000);
  auto after = f.read(2);
  for (unsigned n = 0; n < 8; ++n) {
    equal(after[n], before[n]);
    equal(f.read(1)[n], n + 1);
  }
  equal(after[8], 0x80);
  f.rtc.power();
  equal(f.rtc.running(), false);
  f.write(0, {});
  equal(f.rtc.running(), true);
  f.advance(187500000);
  equal(f.read(2)[0], 1);
  f.write(255, {1, 2, 3, 4, 5, 6, 7, 8});
  for (unsigned n = 0; n < 8; ++n)
    equal(f.read(3)[n], n + 1);
}

void persistence() {
  ClockFixture f;
  f.write(1, {1, 2, 3, 4, 5, 6, 7, 8});
  f.write(2, {0x59, 0x59, 0xa3, 0x31, 0x06, 0x12, 0x99, 0});
  const auto storage = f.rtc.save();
  std::uint64_t timestamp = 0;
  for (unsigned n = 24; n < 32; ++n)
    timestamp = (timestamp << 8) | storage[n];
  equal(timestamp, static_cast<std::uint64_t>(f.now));
  f.now += 62;
  EventQueue events;
  Rtc restored(events, false, [&] { return f.now; });
  equal(restored.load(std::span(storage).first(31)), false);
  equal(restored.present(), false);
  equal(restored.load(storage), true);
  std::array<std::uint8_t, 2> command{7, 2};
  std::array<std::uint8_t, 9> response{};
  restored.communicate(command, response);
  constexpr std::array<std::uint8_t, 8> date{1, 1, 0x80, 1, 0, 1, 0, 1};
  for (unsigned n = 0; n < 8; ++n)
    equal(response[n], date[n]);
  equal(events.time_to_event(), 187500000);
  f.now -= 1000;
  equal(restored.load(storage), true);
  restored.communicate(command, response);
  equal(response[0], 0x59);
  equal(response[3], 0x31);
  command[1] = 1;
  restored.communicate(command, response);
  for (unsigned n = 0; n < 8; ++n)
    equal(response[n], n + 1);
  const auto time = static_cast<std::time_t>(f.now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  Rtc::Storage fresh;
  fresh.fill(255);
  equal(restored.load(fresh), true);
  command[1] = 2;
  restored.communicate(command, response);
  const auto bcd = [](unsigned n) { return (n / 10 << 4) | (n % 10); };
  equal(response[0], bcd(static_cast<unsigned>(local.tm_sec)));
  equal(response[1], bcd(static_cast<unsigned>(local.tm_min)));
  equal(response[2], bcd(static_cast<unsigned>(local.tm_hour)) | 0x80);
}

void cartridge_packets() {
  EventQueue events;
  Eeprom eeprom(events, 512);
  Rtc rtc(events, true, [] { return 1700000000; });
  CartridgeJoybus cartridge(eeprom, rtc);
  std::array<std::uint8_t, 1> command{0};
  std::array<std::uint8_t, 3> response{};
  equal(cartridge.communicate(command, response).valid, true);
  equal(response[1], 0x80);
  command[0] = 6;
  equal(cartridge.communicate(command, response).valid, true);
  equal(response[1], 0x10);
  Eeprom absent(events, 0);
  CartridgeJoybus only_clock(absent, rtc);
  equal(only_clock.communicate(command, response).valid, true);
  command[0] = 0;
  equal(only_clock.communicate(command, response).valid, false);
  ConsoleConfig config;
  config.rtc_present = true;
  config.rtc_clock = [] { return 1700000000; };
  auto console = std::make_unique<Console>(config);
  command[0] = 6;
  auto &pif = console->pif();
  std::fill(pif.ram().begin(), pif.ram().end(), 0);
  pif.ram()[4] = 1;
  pif.ram()[5] = 3;
  pif.ram()[6] = 6;
  pif.ram()[10] = 0xfe;
  pif.write_word(0x1fc007fc, 1);
  pif.dma_read(0x1fc007c0, 0);
  equal(pif.ram()[5], 3);
  equal(pif.ram()[7], 0);
  equal(pif.ram()[8], 0x10);
  equal(pif.ram()[9], 0);
  std::array<std::uint8_t, 10> write{8, 2, 0, 0, 0x80, 1, 0, 1, 0x25, 1};
  std::array<std::uint8_t, 1> status{};
  console->rtc().communicate(write, status);
  console->cpu().state().clocks += 187500000;
  console->synchronize();
  std::array<std::uint8_t, 2> read{7, 2};
  std::array<std::uint8_t, 9> date{};
  console->rtc().communicate(read, date);
  equal(date[0], 1);
  console->power();
  console->cpu().state().clocks += 187500000;
  console->synchronize();
  console->rtc().communicate(read, date);
  equal(date[0], 2);
}

} // namespace

void rtc_tests() {
  packet_lengths();
  calendar();
  control_and_locks();
  persistence();
  cartridge_packets();
}

} // namespace test
