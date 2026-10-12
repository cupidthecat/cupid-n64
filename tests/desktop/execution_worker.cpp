#include "desktop/timing/execution_worker.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <future>
#include <stdexcept>

using cupid::desktop::ExecutionWorker;

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
  {
    ExecutionWorker ending;
    ending.submit([&] { ++completed; });
  }
  test::equal(completed.load(), 3);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
