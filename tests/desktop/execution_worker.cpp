#include "desktop/timing/execution_worker.hpp"
#include "../support/test.hpp"
#include "desktop/timing/execution_output.hpp"
#include <atomic>
#include <future>
#include <stdexcept>

using cupid::desktop::ExecutionWorker;

namespace {
void continuous_execution() {
  ExecutionWorker worker;
  cupid::desktop::ExecutionOutput<unsigned> output(worker);
  std::atomic<unsigned> fields = 0;
  std::promise<void> fourth_waiting, fourth_completed, fifth_waiting;
  worker.repeat([&] {
    const auto next = fields.load() + 1;
    if (next == 4)
      fourth_waiting.set_value();
    if (next == 5)
      fifth_waiting.set_value();
    if (!output.writable())
      return false;
    fields = next;
    output.publish(next);
    if (next == 4)
      fourth_completed.set_value();
    return true;
  });
  const auto waiting = fourth_waiting.get_future().wait_for(std::chrono::seconds(2));
  test::equal(waiting == std::future_status::ready, true);
  test::equal(fields.load(), 3);
  test::equal(worker.ready(), false);
  const auto first = output.take();
  test::equal(first.value_or(0), 1);
  const auto fourth = fourth_completed.get_future().wait_for(std::chrono::seconds(2));
  test::equal(fourth == std::future_status::ready, true);
  const auto fifth = fifth_waiting.get_future().wait_for(std::chrono::seconds(2));
  test::equal(fifth == std::future_status::ready, true);
  worker.interrupt();
  worker.wait();
  test::equal(fields.load(), 4);
  for (unsigned field = 2; field <= 4; ++field)
    test::equal(output.take().value_or(0), field);
  test::equal(output.take().has_value(), false);

  unsigned resumed = 0;
  worker.repeat([&] {
    output.publish(++resumed);
    return resumed < 3;
  });
  worker.wait();
  test::equal(resumed, 3);
  for (unsigned field = 1; field <= 3; ++field)
    test::equal(output.take().value_or(0), field);

  worker.repeat([&]() -> bool {
    if (++resumed == 5)
      throw std::runtime_error("continuous execution failed");
    output.publish(resumed);
    return true;
  });
  bool failed = false;
  try {
    worker.wait();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  test::equal(failed, true);
  test::equal(output.take().value_or(0), 4);

  std::promise<void> pacing;
  std::atomic<bool> continued = true;
  worker.repeat([&] {
    pacing.set_value();
    continued = worker.wait_for(std::chrono::seconds(10));
    return false;
  });
  pacing.get_future().wait();
  worker.interrupt();
  worker.wait();
  test::equal(continued.load(), false);

  std::promise<void> interval_entered, interval_release;
  auto released = interval_release.get_future();
  worker.repeat([&] {
    if (!output.writable())
      return false;
    interval_entered.set_value();
    released.wait();
    output.publish(99);
    return true;
  });
  interval_entered.get_future().wait();
  worker.interrupt();
  interval_release.set_value();
  worker.wait();
  test::equal(output.take().value_or(0), 99);
  test::equal(output.take().has_value(), false);
}
} // namespace

int main() {
  const auto caller = std::this_thread::get_id();
  std::thread::id executor;
  std::atomic<unsigned> completed = 0;
  ExecutionWorker worker;
  worker.submit([&] {
    executor = std::this_thread::get_id();
    ++completed;
  });
  worker.wait();
  test::equal(executor != caller, true);
  test::equal(completed.load(), 1);
  test::equal(worker.ready(), false);
  std::promise<void> release, entered;
  auto released = release.get_future();
  worker.submit([&] {
    entered.set_value();
    released.wait();
  });
  entered.get_future().wait();
  bool rejected = false;
  try {
    worker.submit([] {});
  } catch (const std::logic_error &) {
    rejected = true;
  }
  test::equal(rejected, true);
  test::equal(worker.ready(), false);
  release.set_value();
  worker.wait();
  worker.submit([] { throw std::runtime_error("interval failed"); });
  bool failed = false;
  try {
    worker.wait();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  test::equal(failed, true);
  worker.submit([&] { ++completed; });
  worker.wait();
  test::equal(completed.load(), 2);
  std::promise<void> interruptible;
  worker.submit([&] {
    interruptible.set_value();
    while (!worker.interrupted())
      std::this_thread::yield();
    ++completed;
  });
  interruptible.get_future().wait();
  worker.interrupt();
  worker.wait();
  test::equal(completed.load(), 3);
  worker.submit([&] { test::equal(worker.interrupted(), false); });
  worker.wait();
  {
    ExecutionWorker ending;
    ending.submit([&] { ++completed; });
  }
  test::equal(completed.load(), 4);
  continuous_execution();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
