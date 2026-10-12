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
