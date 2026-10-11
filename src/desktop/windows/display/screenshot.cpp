#include "desktop/video/bitmap.hpp"
#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/window.hpp"
#include <stdexcept>

namespace cupid::desktop {

void Window::screenshot() {
  if (pixels_.empty())
    return;
  try {
    const auto image = bitmap_image(pixel_width_, pixel_height_, pixels_);
    const auto filename =
        game_image_.filename().wstring() + L".frame-" + std::to_wstring(last_frame_) + L".bmp";
    const auto directory = session_settings_.storage.directory(
        StorageLocation::Screenshots, game_image_, application_directory());
    validate_directory(directory, true);
    const auto destination = select_file(window_, L"Save the displayed game image",
                                         FileSelection::Screenshot, directory, filename);
    if (!destination) {
      rebase();
      return;
    }
    write_save(*destination, image);
    state_message_ = L"Screenshot saved: " + std::filesystem::absolute(*destination).wstring();
  } catch (const std::exception &error) {
    status_error(L"Screenshot failed", error);
  }
  rebase();
}

} // namespace cupid::desktop
