#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace cupid::desktop {

// The caller joins the owned task before changing its session.
class ExecutionWorker {
public:
  ExecutionWorker();
  ~ExecutionWorker();
  ExecutionWorker(const ExecutionWorker &) = delete;
  ExecutionWorker &operator=(const ExecutionWorker &) = delete;
  void notify(std::function<void()> callback);
  void submit(std::function<void()> task);
  void repeat(std::function<bool()> iteration);
  bool ready() const;
  void wait();
  void interrupt() noexcept;
  bool wait_until(const std::function<bool()> &resume);
  bool wait_for(std::chrono::milliseconds duration);
  void wake();
  bool interrupted() const noexcept {
    return interrupted_.load(std::memory_order_relaxed);
  }

private:
  void run();
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::function<void()> task_, notify_;
  std::exception_ptr error_;
  bool pending_ = false, done_ = false, stopping_ = false;
  std::atomic<bool> interrupted_{false};
  std::thread thread_;
};

} // namespace cupid::desktop
