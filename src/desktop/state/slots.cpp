#include "desktop/state/slots.hpp"
#include "desktop/storage/save.hpp"
#include <stdexcept>
#include <utility>

namespace cupid::desktop {
namespace {

void require_slot(unsigned slot) {
  if (slot < 1 || slot > StateSlots::count)
    throw std::out_of_range("Select a save-state slot from 1 to 10.");
}

void require_snapshot(std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > StateSlots::maximum_bytes)
    throw std::runtime_error("The machine state has an invalid size.");
}

} // namespace

StateSlots::StateSlots(const std::filesystem::path &image, Capture capture, Restore restore)
    : directory_(image), capture_(std::move(capture)), restore_(std::move(restore)) {
  directory_ += L".states";
}

void StateSlots::select(unsigned slot) {
  require_slot(slot);
  selected_ = slot;
}

unsigned StateSlots::selected() const {
  return selected_;
}

std::filesystem::path StateSlots::path(unsigned slot) const {
  require_slot(slot);
  return directory_ / ("slot-" + std::to_string(slot) + ".state");
}

bool StateSlots::occupied(unsigned slot) const {
  return std::filesystem::is_regular_file(path(slot));
}

void StateSlots::save() {
  PreviousFile previous{selected_, occupied(selected_), {}};
  const auto destination = path(selected_);
  if (previous.existed)
    previous.bytes = read_file(destination, maximum_bytes);
  const auto bytes = capture_();
  require_snapshot(bytes);
  std::filesystem::create_directories(directory_);
  write_save(destination, bytes);
  previous_file_ = std::move(previous);
}

void StateSlots::load() {
  const auto bytes = read_file(path(selected_), maximum_bytes);
  require_snapshot(bytes);
  auto previous = capture_();
  require_snapshot(previous);
  restore_(bytes);
  previous_machine_ = std::move(previous);
}

void StateSlots::undo_save() {
  if (!previous_file_)
    throw std::runtime_error("There is no save-state write to undo.");
  const auto destination = path(previous_file_->slot);
  if (previous_file_->existed)
    write_save(destination, previous_file_->bytes);
  else if (!std::filesystem::remove(destination))
    throw std::runtime_error("Could not remove the saved state.");
  previous_file_.reset();
}

void StateSlots::undo_load() {
  if (!previous_machine_)
    throw std::runtime_error("There is no save-state load to undo.");
  restore_(*previous_machine_);
  previous_machine_.reset();
}

std::optional<unsigned> StateSlots::undo_save_slot() const {
  return previous_file_ ? std::optional(previous_file_->slot) : std::nullopt;
}

bool StateSlots::can_undo_load() const {
  return previous_machine_.has_value();
}

} // namespace cupid::desktop
