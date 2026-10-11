#include "desktop/video/bitmap.hpp"
#include "desktop/windows/window.hpp"
#include <algorithm>
#include <commdlg.h>
#include <stdexcept>

namespace cupid::desktop {

void Window::screenshot() {
  if (pixels_.empty())
    return;
  try {
    const auto image = bitmap_image(pixel_width_, pixel_height_, pixels_);
    const auto filename =
        game_image_.filename().wstring() + L".frame-" + std::to_wstring(last_frame_) + L".bmp";
    const auto initial = (game_image_.parent_path() / filename).wstring();
    std::array<wchar_t, 32768> path{};
    if (initial.size() >= path.size())
      throw std::runtime_error("The screenshot filename is too long.");
    std::copy(initial.begin(), initial.end(), path.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = L"Save the displayed game image";
    dialog.lpstrFilter = L"Bitmap image (*.bmp)\0*.bmp\0";
    dialog.lpstrDefExt = L"bmp";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
      if (CommDlgExtendedError())
        throw std::runtime_error("Could not open the screenshot file picker.");
      rebase();
      return;
    }
    const std::filesystem::path destination(path.data());
    write_save(destination, image);
    state_message_ = L"Screenshot saved: " + std::filesystem::absolute(destination).wstring();
  } catch (const std::exception &error) {
    status_error(L"Screenshot failed", error);
  }
  rebase();
}

} // namespace cupid::desktop
