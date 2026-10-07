#include "core/timing/event_queue.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void timing_tests() {
  {
    EventQueue queue;
    unsigned notifications = 0;
    std::int32_t deadline = 0;
    queue.connect_insert([&] {
      ++notifications;
      deadline = queue.time_to_event();
    });
    queue.insert(Event::SerialRead, 42);
    equal(notifications, 1);
    equal(deadline, 42);
    queue.insert(Event::SerialWrite, 7);
    equal(notifications, 2);
    equal(deadline, 7);
    queue.reset();
    for (unsigned n = 0; n < 512; ++n)
      queue.insert(Event::SerialRead, 100);
    equal(notifications, 514);
    queue.insert(Event::SerialRead, 0);
    equal(notifications, 514);
    equal(deadline, 100);
  }
  {
    EventQueue queue;
    std::vector<Event> events;
    queue.insert(Event::PeripheralRead, 30);
    queue.insert(Event::SerialRead, 10);
    queue.insert(Event::PeripheralWrite, 20);
    equal(queue.time_to_event(), 10);
    queue.advance(10, [&](Event event) { events.push_back(event); });
    equal(events.size(), 1);
    equal(static_cast<unsigned>(events[0]), static_cast<unsigned>(Event::SerialRead));
    equal(queue.cancel(Event::PeripheralRead), 20);
    queue.advance(20, [&](Event event) { events.push_back(event); });
    equal(events.size(), 2);
    equal(static_cast<unsigned>(events[1]), static_cast<unsigned>(Event::PeripheralWrite));
    equal(queue.time_to_event(), 0x7fffffff);
    queue.insert(Event::SerialRead, 0);
    queue.advance(0, [&](Event event) {
      events.push_back(event);
      queue.insert(Event::SerialWrite, 5);
    });
    equal(queue.time_to_event(), 5);
    queue.advance(5, [&](Event event) { events.push_back(event); });
    equal(events.size(), 4);
  }
  {
    EventQueue queue;
    queue.advance(0xfffffff0, [](Event) {});
    queue.insert(Event::PeripheralRead, 32);
    equal(queue.time_to_event(), 32);
    unsigned count = 0;
    queue.advance(31, [&](Event) { ++count; });
    equal(count, 0);
    queue.advance(1, [&](Event) { ++count; });
    equal(count, 1);
    for (unsigned n = 0; n < 512; ++n)
      equal(queue.insert(Event::PeripheralRead, 1000), true);
    equal(queue.insert(Event::PeripheralRead, 1000), false);
    queue.cancel(Event::PeripheralRead);
    equal(queue.insert(Event::PeripheralRead, 1), false);
    queue.advance(1000, [&](Event) { ++count; });
    equal(count, 1);
    equal(queue.insert(Event::PeripheralRead, 1), true);
    queue.reset();
    equal(queue.time_to_event(), 0x7fffffff);
  }
  {
    EventQueue queue;
    queue.advance(0xfffffff0, [](Event) {});
    queue.insert(Event::SerialRead, 30);
    queue.insert(Event::RtcTick, 10);
    queue.insert(Event::SerialWrite, 20);
    queue.insert(Event::RtcTick, 40);
    queue.insert(Event::ClockTick, 15);
    queue.remove(Event::RtcTick);
    equal(queue.time_to_event(), 15);
    std::vector<Event> fired;
    queue.advance(15, [&](Event event) { fired.push_back(event); });
    equal(fired.size(), 1);
    equal(static_cast<unsigned>(fired[0]), static_cast<unsigned>(Event::ClockTick));
    equal(queue.time_to_event(), 5);
    queue.advance(15, [&](Event event) { fired.push_back(event); });
    equal(fired.size(), 3);
    equal(static_cast<unsigned>(fired[1]), static_cast<unsigned>(Event::SerialWrite));
    equal(static_cast<unsigned>(fired[2]), static_cast<unsigned>(Event::SerialRead));
    for (unsigned n = 0; n < 512; ++n)
      equal(queue.insert(Event::RtcTick, 1000), true);
    queue.remove(Event::RtcTick);
    equal(queue.time_to_event(), 0x7fffffff);
    equal(queue.insert(Event::SerialRead, 1), true);
  }
}

} // namespace test
