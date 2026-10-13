#include "desktop/video/presentation_worker.hpp"
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

PresentationWorker::PresentationWorker(Backend backend) : backend_(std::move(backend)) {
  if (!backend_)
    throw std::invalid_argument("The presentation worker needs a backend.");
}

PresentationWorker::~PresentationWorker() {
  try {
    shutdown();
  } catch (...) {
  }
}

void PresentationWorker::notify(std::function<void()> callback) {
  execution_.notify(std::move(callback));
}

void PresentationWorker::submit(Submission submission, Delivered delivered) {
  if (pending_ || closing_)
    throw std::logic_error("The presentation worker already owns an image.");
  if (!delivered)
    throw std::invalid_argument("The presentation worker needs a completion handler.");
  submission.tag.generation = generation_;
  submission.tag.sequence = ++sequence_;
  execution_.submit([this, submission = std::move(submission)] {
    Completion completion;
    completion.tag = submission.tag;
    const auto started = std::chrono::steady_clock::now();
    try {
      completion.result = backend_(submission);
    } catch (...) {
      completion.failure = std::current_exception();
    }
    completion.completed = std::chrono::steady_clock::now();
    completion.duration = completion.completed - started;
    completion_ = std::move(completion);
  });
  delivered_ = std::move(delivered);
  pending_ = true;
}

bool PresentationWorker::pending() const {
  return pending_;
}

bool PresentationWorker::poll() {
  if (!pending_ || !execution_.ready())
    return false;
  deliver();
  return true;
}

void PresentationWorker::quiesce() {
  std::exception_ptr failure;
  while (pending_) {
    try {
      deliver();
    } catch (...) {
      if (!failure)
        failure = std::current_exception();
    }
  }
  if (failure)
    std::rethrow_exception(failure);
}

void PresentationWorker::deliver() {
  execution_.wait();
  pending_ = false;
  auto completion = std::move(*completion_);
  completion_.reset();
  auto delivered = std::exchange(delivered_, {});
  delivered(completion);
  if (completion.failure)
    std::rethrow_exception(completion.failure);
}

void PresentationWorker::new_generation() {
  quiesce();
  ++generation_;
}

void PresentationWorker::shutdown() {
  // An accepted graphics transaction releases its context before completing.
  execution_.interrupt();
  closing_ = true;
  std::exception_ptr failure;
  try {
    quiesce();
  } catch (...) {
    failure = std::current_exception();
  }
  execution_.shutdown();
  if (failure)
    std::rethrow_exception(failure);
}

} // namespace cupid::desktop
