#include "desktop/timing/execution_worker.hpp"
#include <stdexcept>
#include <utility>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace cupid::desktop {

ExecutionWorker::ExecutionWorker() : thread_([this] { run(); }) {}

ExecutionWorker::~ExecutionWorker() {
  interrupt();
  try {
    wait();
  } catch (...) {
  }
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  changed_.notify_all();
  thread_.join();
}

void ExecutionWorker::notify(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  notify_ = std::move(callback);
}

void ExecutionWorker::submit(std::function<void()> task) {
  std::lock_guard lock(mutex_);
  if (!task || pending_ || stopping_)
    throw std::logic_error("The execution worker already owns an interval.");
  task_ = std::move(task);
  pending_ = true;
  done_ = false;
  interrupted_.store(false, std::memory_order_relaxed);
  changed_.notify_all();
}

void ExecutionWorker::repeat(std::function<bool()> iteration) {
  if (!iteration)
    throw std::logic_error("The execution worker needs an iteration.");
  submit([this, iteration = std::move(iteration)] {
    while (!interrupted() && iteration()) {
    }
  });
}

void ExecutionWorker::interrupt() noexcept {
  {
    std::lock_guard lock(mutex_);
    interrupted_.store(true, std::memory_order_relaxed);
  }
  changed_.notify_all();
}

bool ExecutionWorker::wait_until(const std::function<bool()> &resume) {
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [&] { return interrupted() || resume(); });
  return !interrupted();
}

bool ExecutionWorker::wait_for(std::chrono::milliseconds duration) {
  std::unique_lock lock(mutex_);
  changed_.wait_for(lock, duration, [this] { return interrupted(); });
  return !interrupted();
}

void ExecutionWorker::wake() {
  std::lock_guard lock(mutex_);
  changed_.notify_all();
}

bool ExecutionWorker::ready() const {
  std::lock_guard lock(mutex_);
  return pending_ && done_;
}

void ExecutionWorker::wait() {
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [this] { return !pending_ || done_; });
  const auto error = std::exchange(error_, {});
  pending_ = done_ = false;
  lock.unlock();
  if (error)
    std::rethrow_exception(error);
}

void ExecutionWorker::run() {
#if defined(_WIN32)
  SetThreadDescription(GetCurrentThread(), L"Emulation");
#endif
  for (;;) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return stopping_ || bool(task_); });
    if (stopping_)
      return;
    const auto task = std::exchange(task_, {});
    lock.unlock();
    std::exception_ptr error;
    try {
      task();
    } catch (...) {
      error = std::current_exception();
    }
    lock.lock();
    error_ = error;
    done_ = true;
    const auto notify = notify_;
    lock.unlock();
    changed_.notify_all();
    if (notify)
      notify();
  }
}

} // namespace cupid::desktop
