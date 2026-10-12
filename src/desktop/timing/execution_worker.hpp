#pragma once

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

private:
  void run();
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::function<void()> task_, notify_;
  std::exception_ptr error_;
  bool pending_ = false, done_ = false, stopping_ = false;
  std::thread thread_;
};

} // namespace cupid::desktop
