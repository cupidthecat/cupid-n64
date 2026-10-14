#include "desktop/video/presentation_worker.hpp"
#include "../../support/test.hpp"
#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <string_view>

using cupid::desktop::PresentationWorker;
using Submission = PresentationWorker::Submission;
using Completion = PresentationWorker::Completion;
using Result = PresentationWorker::Result;
using namespace std::chrono_literals;

namespace {

template <typename Exception> bool rejects(auto operation, std::string_view message = {}) {
  try {
    operation();
  } catch (const Exception &error) {
    return message.empty() || error.what() == message;
  }
  return false;
}

Submission image(unsigned field) {
  Submission submission;
  submission.tag = {999, 999, field};
  submission.pixels = {4, 3, 2, 1};
  submission.width = submission.height = 1;
  submission.surface = {320, 240, cupid::desktop::DisplayScale::Fit,
                        cupid::desktop::DisplayAspect::Standard, true};
  return submission;
}

void immutable_ordered_images() {
  const auto caller = std::this_thread::get_id();
  std::promise<void> entered, release, notified;
  auto entered_future = entered.get_future();
  auto release_future = release.get_future().share();
  auto notified_future = notified.get_future();
  std::atomic<unsigned> notifications = 0;
  std::thread::id backend_thread;
  std::vector<std::thread::id> backend_threads;
  std::vector<Submission> requests;
  PresentationWorker worker([&](const Submission &submission) {
    if (requests.empty()) {
      backend_thread = std::this_thread::get_id();
      entered.set_value();
      if (release_future.wait_for(2s) != std::future_status::ready)
        throw std::runtime_error("The held image was not released.");
    }
    backend_threads.push_back(std::this_thread::get_id());
    requests.push_back(submission);
    return Result{true, true, {}, submission.pixels};
  });
  worker.notify([&] {
    if (++notifications == 1)
      notified.set_value();
  });
  std::vector<Completion> delivered;
  const auto receive = [&](const Completion &completion) {
    test::equal(std::this_thread::get_id() == caller, true);
    test::equal(worker.pending(), false);
    delivered.push_back(completion);
  };
  auto submission = image(7);
  const auto expected = submission.pixels;
  const auto started = std::chrono::steady_clock::now();
  worker.submit(submission, receive);
  test::equal(entered_future.wait_for(2s) == std::future_status::ready, true);
  submission.pixels.assign(4, 255);
  submission.surface.width = 900;
  test::equal(worker.pending(), true);
  test::equal(worker.poll(), false);
  test::equal(delivered.empty(), true);
  test::equal(rejects<std::logic_error>([&] { worker.submit(image(8), receive); }), true);
  release.set_value();
  test::equal(notified_future.wait_for(2s) == std::future_status::ready, true);
  test::equal(worker.poll(), true);
  test::equal(worker.poll(), false);
  test::equal(backend_thread != caller, true);
  test::equal(requests.size(), 1);
  test::equal(requests[0].pixels == expected, true);
  test::equal(requests[0].surface.width, 320);
  test::equal(requests[0].surface.blocking, true);
  test::equal(delivered.size(), 1);
  test::equal(delivered[0].tag == PresentationWorker::Tag{1, 1, 7}, true);
  test::equal(delivered[0].result.captured == expected, true);
  test::equal(delivered[0].result.presented && delivered[0].result.available, true);
  test::equal(delivered[0].completed >= started, true);
  test::equal(delivered[0].duration.count() >= 0, true);
  worker.submit(image(7), receive);
  worker.quiesce();
  worker.new_generation();
  worker.submit(image(7), receive);
  worker.quiesce();
  test::equal(delivered.size(), 3);
  test::equal(delivered[1].tag == PresentationWorker::Tag{1, 2, 7}, true);
  test::equal(delivered[2].tag == PresentationWorker::Tag{2, 3, 7}, true);
  test::equal(requests[1].tag == delivered[1].tag, true);
  test::equal(requests[2].tag == delivered[2].tag, true);
  auto capture = image(7);
  capture.operation = Submission::Operation::Capture;
  worker.submit(std::move(capture), receive);
  worker.quiesce();
  Submission release_graphics;
  release_graphics.operation = Submission::Operation::Release;
  worker.submit(std::move(release_graphics), receive);
  worker.quiesce();
  test::equal(requests[3].operation == Submission::Operation::Capture, true);
  test::equal(requests[4].operation == Submission::Operation::Release, true);
  test::equal(delivered[4].tag == PresentationWorker::Tag{2, 5, 0}, true);
  test::equal(std::all_of(backend_threads.begin(), backend_threads.end(),
                          [&](auto thread) { return thread == backend_thread; }),
              true);
  worker.shutdown();
  test::equal(notifications.load(), 5);
}

void reentrant_delivery_and_failures() {
  const auto caller = std::this_thread::get_id();
  std::vector<unsigned> executed, delivered;
  PresentationWorker worker([&](const Submission &submission) {
    executed.push_back(submission.tag.field);
    if (submission.tag.field == 3)
      throw std::runtime_error("backend failed");
    if (submission.tag.field == 6)
      return Result{false, false, "presentation failed", {}};
    return Result{true, true, {}, submission.pixels};
  });
  const auto receive = [&](const Completion &completion) {
    test::equal(std::this_thread::get_id() == caller, true);
    test::equal(worker.pending(), false);
    delivered.push_back(completion.tag.field);
  };
  worker.submit(image(1), [&](const Completion &completion) {
    receive(completion);
    test::equal(worker.poll(), false);
    worker.submit(image(2), receive);
  });
  worker.quiesce();
  test::equal(delivered == std::vector<unsigned>{1, 2}, true);
  test::equal(worker.pending(), false);
  worker.submit(image(3), [&](const Completion &completion) {
    receive(completion);
    test::equal(bool(completion.failure), true);
  });
  test::equal(rejects<std::runtime_error>([&] { worker.quiesce(); }, "backend failed"), true);
  test::equal(worker.pending(), false);
  worker.submit(image(4), [&](const Completion &completion) {
    receive(completion);
    worker.submit(image(5), receive);
    throw std::runtime_error("delivery failed");
  });
  test::equal(rejects<std::runtime_error>([&] { worker.quiesce(); }, "delivery failed"), true);
  test::equal(worker.pending(), false);
  worker.submit(image(6), [&](const Completion &completion) {
    receive(completion);
    test::equal(bool(completion.failure), false);
    test::equal(completion.result.presented || completion.result.available, false);
    test::equal(completion.result.error == "presentation failed", true);
  });
  worker.quiesce();
  worker.submit(image(7), receive);
  worker.quiesce();
  test::equal(delivered == std::vector<unsigned>{1, 2, 3, 4, 5, 6, 7}, true);
  test::equal(executed == delivered, true);
}

void held_shutdown(bool destroy) {
  const auto caller = std::this_thread::get_id();
  std::promise<void> entered, release, stopping;
  auto entered_future = entered.get_future();
  auto release_future = release.get_future().share();
  auto stopping_future = stopping.get_future();
  std::atomic<bool> returned = false, delivered = false;
  auto worker = std::make_unique<PresentationWorker>([&](const Submission &submission) {
    entered.set_value();
    if (release_future.wait_for(2s) != std::future_status::ready)
      throw std::runtime_error("The held shutdown was not released.");
    return Result{true, true, {}, submission.pixels};
  });
  auto *owner = worker.get();
  owner->submit(image(9), [&](const Completion &completion) {
    test::equal(std::this_thread::get_id() == caller, true);
    test::equal(completion.tag.field, 9);
    test::equal(completion.duration >= 10ms, true);
    test::equal(owner->pending(), false);
    test::equal(rejects<std::logic_error>([&] { owner->submit(image(10), [](const auto &) {}); }),
                true);
    delivered = true;
  });
  test::equal(entered_future.wait_for(2s) == std::future_status::ready, true);
  auto coordinator = std::async(std::launch::async, [&] {
    stopping_future.wait();
    std::this_thread::sleep_for(20ms);
    const bool waited = !returned.load() && !delivered.load();
    release.set_value();
    return waited;
  });
  stopping.set_value();
  if (destroy)
    worker.reset();
  else
    worker->shutdown();
  returned = true;
  test::equal(coordinator.get(), true);
  test::equal(delivered.load(), true);
  if (!destroy) {
    worker->shutdown();
    test::equal(rejects<std::logic_error>([&] { worker->submit(image(11), [](const auto &) {}); }),
                true);
  }
}

void failing_shutdown_delivery() {
  PresentationWorker worker([](const auto &) { return Result{true, true, {}, {}}; });
  unsigned delivered = 0;
  worker.submit(image(12), [&](const Completion &) {
    ++delivered;
    test::equal(worker.pending(), false);
    test::equal(rejects<std::logic_error>([&] { worker.submit(image(13), [](const auto &) {}); }),
                true);
    throw std::runtime_error("shutdown delivery failed");
  });
  test::equal(rejects<std::runtime_error>([&] { worker.shutdown(); }, "shutdown delivery failed"),
              true);
  test::equal(delivered, 1);
  test::equal(worker.pending(), false);
  worker.shutdown();
}

} // namespace

int main() {
  immutable_ordered_images();
  reentrant_delivery_and_failures();
  held_shutdown(false);
  held_shutdown(true);
  failing_shutdown_delivery();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
