#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace cupid::desktop {

// One owned execution interval at a time. The caller joins it before changing the session.
class ExecutionWorker {
public:
  ExecutionWorker();
  ~ExecutionWorker();
  ExecutionWorker(const ExecutionWorker &) = delete;
  ExecutionWorker &operator=(const ExecutionWorker &) = delete;
  void notify(std::function<void()> callback);
  void submit(std::function<void()> task);
  bool ready() const;
  void wait();
  void interrupt() noexcept {
    interrupted_.store(true, std::memory_order_relaxed);
  }
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
