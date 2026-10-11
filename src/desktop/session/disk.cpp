#include "core/disk/image/image.hpp"
#include "core/state/archive.hpp"
#include "desktop/session.hpp"
#include <stdexcept>

namespace cupid::desktop {

bool Session::has_disk_drive() const {
  return console_->disk_drive().firmware_loaded();
}

void Session::change_disk(const std::filesystem::path &path) {
  if (!has_disk_drive())
    throw std::runtime_error("Load a 64DD session before changing disks.");
  const auto source = read_file(path, 0x435b0c0);
  n64::DiskImage image;
  if (!image.load(source))
    throw std::runtime_error("Select a valid 64DD disk image.");
  const auto identity = n64::state::Archive::fingerprint(source);
  auto next_image = path;
  auto next_storage = settings_.storage.save_image(path, identity);
  auto next_path = save_path(next_storage, L".disk");
  if (identity == disk_identity_ && next_path == disk_path_) {
    const auto current = console_->disk_drive().disk_data();
    image.data.assign(current.begin(), current.end());
  } else if (std::filesystem::exists(next_path)) {
    auto saved = read_file(next_path, image.data.size());
    if (saved.size() != image.data.size())
      throw std::runtime_error("The disk save has an invalid size.");
    image.data = std::move(saved);
  }
  auto next_saved = image.data;
  save();
  if (!console_->disk_drive().insert(image.data, image.errors))
    throw std::runtime_error("The disk image could not be inserted.");
  disk_image_ = std::move(next_image);
  disk_path_ = std::move(next_path);
  saved_disk_ = std::move(next_saved);
  disk_identity_ = identity;
  if (cartridge_image_.empty())
    storage_image_ = std::move(next_storage);
}

void Session::eject_disk() {
  if (!has_disk_drive())
    throw std::runtime_error("Load a 64DD session before ejecting a disk.");
  auto next_storage = cartridge_image_.empty()
                          ? settings_.storage.save_image(ipl_image_, ipl_identity_)
                          : storage_image_;
  save();
  console_->disk_drive().eject();
  disk_image_.clear();
  disk_path_.clear();
  saved_disk_.clear();
  disk_identity_ = 0;
  storage_image_ = std::move(next_storage);
}

} // namespace cupid::desktop
