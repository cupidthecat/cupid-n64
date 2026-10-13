#pragma once

#include "desktop/timing/execution_worker.hpp"
#include "desktop/video/viewport.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cupid::desktop {

struct VideoSurface {
  unsigned width = 0, height = 0;
  DisplayScale scale = DisplayScale::Fit;
  DisplayAspect aspect = DisplayAspect::Standard;
  bool blocking = false;
};

class PresentationWorker {
public:
  struct Tag {
    std::uint64_t generation = 0, sequence = 0;
    unsigned field = 0;
    bool operator==(const Tag &) const = default;
  };
  struct Submission {
    enum class Operation { Present, Capture, Release };
    Operation operation = Operation::Present;
    Tag tag;
    std::vector<std::uint8_t> pixels;
    unsigned width = 0, height = 0;
    VideoSurface surface;
  };
  struct Result {
    bool presented = false, available = false;
    std::string error;
    std::vector<std::uint8_t> captured;
  };
  struct Completion {
    Tag tag;
    Result result;
    std::exception_ptr failure;
    std::chrono::nanoseconds duration{};
    std::chrono::steady_clock::time_point completed;
  };
  using Backend = std::function<Result(const Submission &)>;
  using Delivered = std::function<void(const Completion &)>;

  explicit PresentationWorker(Backend backend);
  ~PresentationWorker();
  PresentationWorker(const PresentationWorker &) = delete;
  PresentationWorker &operator=(const PresentationWorker &) = delete;
  void notify(std::function<void()> callback);
  void submit(Submission submission, Delivered delivered);
  bool pending() const;
  bool poll();
  void quiesce();
  void new_generation();
  void shutdown();

private:
  void deliver();
  Backend backend_;
  Delivered delivered_;
  std::optional<Completion> completion_;
  bool pending_ = false, closing_ = false;
  std::uint64_t generation_ = 1, sequence_ = 0;
  ExecutionWorker execution_{L"Presentation"};
};

} // namespace cupid::desktop
