#pragma once

#include "desktop/timing/execution_worker.hpp"
#include <deque>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

// One producer reserves space before execution, then publishes each completed output.
template <typename Output> class ExecutionOutput {
public:
  explicit ExecutionOutput(ExecutionWorker &worker, unsigned capacity = 3)
      : worker_(worker), capacity_(capacity) {
    if (!capacity)
      throw std::logic_error("The execution output needs storage.");
  }

  bool writable() {
    return worker_.wait_until([this] {
      std::lock_guard lock(mutex_);
      return outputs_.size() < capacity_;
    });
  }

  void publish(Output output) {
    std::lock_guard lock(mutex_);
    outputs_.push_back(std::move(output));
  }

  std::optional<Output> take() {
    std::optional<Output> output;
    {
      std::lock_guard lock(mutex_);
      if (outputs_.empty())
        return {};
      output.emplace(std::move(outputs_.front()));
      outputs_.pop_front();
    }
    worker_.wake();
    return output;
  }

private:
  ExecutionWorker &worker_;
  const unsigned capacity_;
  std::mutex mutex_;
  std::deque<Output> outputs_;
};

} // namespace cupid::desktop
