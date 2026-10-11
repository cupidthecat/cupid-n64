#include "desktop/state/rewind.hpp"
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

RewindHistory::RewindHistory(Capture capture, Restore restore, Settings settings)
    : capture_(std::move(capture)), restore_(std::move(restore)) {
  configure(settings);
}

void RewindHistory::configure(Settings settings) {
  if (!settings.interval || settings.interval > 120 || !settings.capacity ||
      settings.capacity > 320 || !settings.memory || settings.memory > 1024ull * 1024 * 1024)
    throw std::invalid_argument("Rewind settings exceed the supported limits.");
  if (settings != settings_) {
    settings_ = settings;
    clear();
  }
}

void RewindHistory::clear() {
  history_.clear();
  recorded_.reset();
  memory_ = 0;
}

bool RewindHistory::record(std::uint64_t frame) {
  if (recorded_ && frame >= *recorded_ && frame - *recorded_ < settings_.interval)
    return false;
  auto bytes = capture_();
  if (bytes.empty())
    throw std::runtime_error("The rewind snapshot is empty.");
  if (bytes.capacity() > settings_.memory)
    throw std::runtime_error("The snapshot exceeds the rewind memory limit.");
  if (recorded_ && frame < *recorded_)
    clear();
  while (!history_.empty() &&
         (history_.size() >= settings_.capacity || memory_ > settings_.memory - bytes.capacity()))
    remove_front();
  history_.push_back({frame, std::move(bytes)});
  memory_ += history_.back().bytes.capacity();
  recorded_ = frame;
  return true;
}

std::optional<std::size_t> RewindHistory::previous(std::uint64_t frame) const {
  for (auto index = history_.size(); index; --index)
    if (history_[index - 1].frame < frame)
      return index - 1;
  return {};
}

bool RewindHistory::can_rewind(std::uint64_t frame) const {
  return previous(frame).has_value();
}

bool RewindHistory::rewind(std::uint64_t frame) {
  const auto index = previous(frame);
  if (!index)
    return false;
  restore_(history_[*index].bytes);
  while (history_.size() > *index + 1) {
    memory_ -= history_.back().bytes.capacity();
    history_.pop_back();
  }
  recorded_ = history_.back().frame;
  return true;
}

void RewindHistory::remove_front() {
  memory_ -= history_.front().bytes.capacity();
  history_.pop_front();
}

std::size_t RewindHistory::size() const {
  return history_.size();
}

std::size_t RewindHistory::memory() const {
  return memory_;
}

} // namespace cupid::desktop
