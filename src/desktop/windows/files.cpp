#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"
#include <shellapi.h>
#include <stdexcept>

namespace cupid::desktop {

void Window::load_recent() {
  if (settings_.empty())
    return;
  std::vector<std::filesystem::path> paths;
  for (unsigned n = 1; n <= RecentRoms::limit; ++n) {
    std::array<wchar_t, 32768> path{};
    GetPrivateProfileStringW(L"Recent ROMs", std::to_wstring(n).c_str(), L"", path.data(),
                             static_cast<DWORD>(path.size()), settings_.c_str());
    if (path.front())
      paths.emplace_back(path.data());
  }
  recent_.restore(std::move(paths));
}

void Window::save_recent() {
  if (settings_.empty() || options_.frames)
    return;
  for (unsigned n = 0; n < RecentRoms::limit; ++n) {
    const auto *path = n < recent_.paths().size() ? recent_.paths()[n].c_str() : nullptr;
    if (!WritePrivateProfileStringW(L"Recent ROMs", std::to_wstring(n + 1).c_str(), path,
                                    settings_.c_str()))
      throw std::runtime_error("Could not save the recent-ROM list. Check folder permissions.");
  }
}

void Window::update_recent() {
  if (recent_.prune()) {
    try {
      save_recent();
    } catch (const std::exception &error) {
      status_error(L"Recent ROMs could not be saved", error);
    }
  }
  if (!recent_menu_)
    return;
  while (GetMenuItemCount(recent_menu_) > 0)
    DeleteMenu(recent_menu_, 0, MF_BYPOSITION);
  for (unsigned n = 0; n < recent_.paths().size(); ++n) {
    std::wstring path;
    for (wchar_t letter : recent_.paths()[n].wstring()) {
      path.push_back(letter);
      if (letter == L'&')
        path.push_back(letter);
    }
    const auto label = L"&" + std::to_wstring(n + 1) + L" " + path;
    AppendMenuW(recent_menu_, MF_STRING, RecentRomFirst + n, label.c_str());
  }
  if (recent_.paths().empty())
    AppendMenuW(recent_menu_, MF_STRING | MF_GRAYED, 0, L"No recent ROMs");
  AppendMenuW(recent_menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(recent_menu_, MF_STRING | (recent_.paths().empty() ? MF_GRAYED : MF_ENABLED),
              ClearRecentRoms, L"&Clear list");
  DrawMenuBar(window_);
}

void Window::open_rom(const std::filesystem::path &path) {
  auto previous = options_;
  try {
    auto next = options_;
    next.rom = std::filesystem::absolute(path).lexically_normal();
    next.ipl.clear();
    next.disk.clear();
    next.arcade_profile = n64::ArcadeProfile::Disabled;
    next.firmware = firmware_files_.n64;
    if (next.firmware.empty()) {
      next.firmware = choose(true);
      if (next.firmware.empty()) {
        rebase();
        return;
      }
    }
    options_ = std::move(next);
    load();
  } catch (const std::exception &error) {
    options_ = std::move(previous);
    status_error(L"Game load failed", error);
    rebase();
  }
}

bool Window::file_command(unsigned id) {
  const bool recent = id >= RecentRomFirst && id < RecentRomFirst + RecentRoms::limit;
  if (id != Open && id != ClearRecentRoms && !recent)
    return false;
  if (id == Open) {
    const auto path = choose(false);
    if (!path.empty())
      open_rom(path);
    else
      rebase();
  } else if (id == ClearRecentRoms) {
    recent_.clear();
    try {
      save_recent();
    } catch (const std::exception &error) {
      status_error(L"Recent ROMs could not be saved", error);
    }
    update_recent();
  } else if (id - RecentRomFirst < recent_.paths().size()) {
    const auto path = recent_.paths()[id - RecentRomFirst];
    open_rom(path);
    update_recent();
  }
  return true;
}

void Window::drop_rom(WPARAM data) {
  struct Files {
    HDROP drop;
    ~Files() {
      DragFinish(drop);
    }
  } files{reinterpret_cast<HDROP>(data)};
  end_rewind();
  stop_fast_forward();
  keys_.fill(false);
  try {
    if (DragQueryFileW(files.drop, 0xffffffff, nullptr, 0) != 1)
      throw std::runtime_error("Drop one Nintendo 64 ROM file at a time.");
    const auto length = DragQueryFileW(files.drop, 0, nullptr, 0);
    if (!length || length >= 32768)
      throw std::runtime_error("The dropped filename is invalid or too long.");
    std::wstring path(length + 1, L'\0');
    if (DragQueryFileW(files.drop, 0, path.data(), static_cast<UINT>(path.size())) != length)
      throw std::runtime_error("Could not read the dropped filename.");
    path.resize(length);
    open_rom(path);
  } catch (const std::exception &error) {
    status_error(L"Game load failed", error);
  }
  rebase();
}

} // namespace cupid::desktop
