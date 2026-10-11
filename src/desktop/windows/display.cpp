#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {

void Window::load_display_settings() {
  if (settings_.empty())
    return;
  const auto value = [&](const wchar_t *name, unsigned fallback, unsigned minimum,
                         unsigned maximum) {
    const auto result = GetPrivateProfileIntW(L"Display", name, fallback, settings_.c_str());
    return result >= minimum && result <= maximum ? result : fallback;
  };
  display_scale_ = static_cast<DisplayScale>(value(L"Scale", 0, 0, 2));
  display_aspect_ = static_cast<DisplayAspect>(value(L"Aspect", 0, 0, 1));
  viewport_size_ = {value(L"Width", 960, 64, 8192), value(L"Height", 720, 64, 8192)};
  std::array<wchar_t, 256> monitor{};
  GetPrivateProfileStringW(L"Display", L"FullscreenMonitor", L"", monitor.data(),
                           static_cast<DWORD>(monitor.size()), settings_.c_str());
  fullscreen_monitor_ = monitor.data();
}

void Window::save_display_settings() {
  if (settings_.empty() || options_.frames)
    return;
  const auto write = [&](const wchar_t *name, unsigned value) {
    return WritePrivateProfileStringW(L"Display", name, std::to_wstring(value).c_str(),
                                      settings_.c_str()) != 0;
  };
  if (!write(L"Scale", static_cast<unsigned>(display_scale_)) ||
      !write(L"Aspect", static_cast<unsigned>(display_aspect_)) ||
      !write(L"Width", viewport_size_.width) || !write(L"Height", viewport_size_.height) ||
      !WritePrivateProfileStringW(L"Display", L"FullscreenMonitor", fullscreen_monitor_.c_str(),
                                  settings_.c_str()))
    throw std::runtime_error("Could not save display settings. Check folder permissions.");
}

void Window::create_display_menu(HMENU view) {
  const auto scale = CreatePopupMenu(), aspect = CreatePopupMenu(), sizes = CreatePopupMenu();
  AppendMenuW(scale, MF_STRING, DisplayFit, L"&Best fit");
  AppendMenuW(scale, MF_STRING, DisplayInteger, L"&Integer scaling");
  AppendMenuW(scale, MF_STRING, DisplayStretch, L"&Stretch to fill");
  AppendMenuW(aspect, MF_STRING, AspectStandard, L"&Standard N64 (4:3)");
  AppendMenuW(aspect, MF_STRING, AspectPixels, L"&Original pixel dimensions");
  for (unsigned n = 0; n < viewport_presets.size(); ++n) {
    const auto &size = viewport_presets[n];
    const auto label = std::to_wstring(n + 1) + L"x: " + std::to_wstring(size.width) + L" x " +
                       std::to_wstring(size.height);
    AppendMenuW(sizes, MF_STRING, ViewportPresetFirst + n, label.c_str());
  }
  AppendMenuW(sizes, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(sizes, MF_STRING, ViewportCustom, L"&Custom viewport...");
  monitor_menu_ = CreatePopupMenu();
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(scale), L"Display &scaling");
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(aspect), L"&Aspect ratio");
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"&Window size");
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(monitor_menu_), L"Fullscreen &monitor");
  refresh_monitors();
}

void Window::refresh_monitors() {
  monitors_.clear();
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto &items = *reinterpret_cast<std::vector<Monitor> *>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (items.size() < 64 && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)))
          items.push_back({info.szDevice, info.rcMonitor, info.rcWork,
                           (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&monitors_));
  if (!fullscreen_monitor_.empty() &&
      std::none_of(monitors_.begin(), monitors_.end(),
                   [&](const Monitor &monitor) { return monitor.device == fullscreen_monitor_; })) {
    fullscreen_monitor_.clear();
    save_display_settings();
  }
  if (monitor_menu_) {
    while (GetMenuItemCount(monitor_menu_) > 0)
      DeleteMenu(monitor_menu_, 0, MF_BYPOSITION);
    AppendMenuW(monitor_menu_, MF_STRING, FullscreenMonitorCurrent, L"&Current window's monitor");
    for (unsigned n = 0; n < monitors_.size(); ++n) {
      const auto &monitor = monitors_[n];
      const auto label = monitor.device + (monitor.primary ? L" (primary)" : L"") + L" | " +
                         std::to_wstring(monitor.bounds.right - monitor.bounds.left) + L" x " +
                         std::to_wstring(monitor.bounds.bottom - monitor.bounds.top);
      AppendMenuW(monitor_menu_, MF_STRING, FullscreenMonitorFirst + n, label.c_str());
    }
  }
  update_display();
}

void Window::update_display() {
  CheckMenuRadioItem(menu_, DisplayFit, DisplayStretch,
                     DisplayFit + static_cast<unsigned>(display_scale_), MF_BYCOMMAND);
  CheckMenuRadioItem(menu_, AspectStandard, AspectPixels,
                     AspectStandard + static_cast<unsigned>(display_aspect_), MF_BYCOMMAND);
  for (unsigned n = 0; n < viewport_presets.size(); ++n) {
    const auto &size = viewport_presets[n];
    CheckMenuItem(menu_, ViewportPresetFirst + n,
                  MF_BYCOMMAND |
                      (size.width == viewport_size_.width && size.height == viewport_size_.height
                           ? MF_CHECKED
                           : MF_UNCHECKED));
  }
  const auto label = L"Custom viewport (" + std::to_wstring(viewport_size_.width) + L" x " +
                     std::to_wstring(viewport_size_.height) + L")...";
  ModifyMenuW(menu_, ViewportCustom, MF_BYCOMMAND | MF_STRING, ViewportCustom, label.c_str());
  if (monitor_menu_) {
    CheckMenuItem(monitor_menu_, FullscreenMonitorCurrent,
                  MF_BYCOMMAND | (fullscreen_monitor_.empty() ? MF_CHECKED : MF_UNCHECKED));
    for (unsigned n = 0; n < monitors_.size(); ++n)
      CheckMenuItem(monitor_menu_, FullscreenMonitorFirst + n,
                    MF_BYCOMMAND |
                        (monitors_[n].device == fullscreen_monitor_ ? MF_CHECKED : MF_UNCHECKED));
  }
  EnableMenuItem(menu_, Screenshot, MF_BYCOMMAND | (!pixels_.empty() ? MF_ENABLED : MF_GRAYED));
  if (window_) {
    DrawMenuBar(window_);
    InvalidateRect(window_, nullptr, FALSE);
  }
}

int Window::status_height() const {
  return MulDiv(24, window_ ? static_cast<int>(GetDpiForWindow(window_)) : 96, 96);
}

void Window::apply_window_size() {
  if (!window_ || fullscreen_)
    return;
  const auto dpi = GetDpiForWindow(window_);
  const int client_height =
      MulDiv(static_cast<int>(viewport_size_.height), static_cast<int>(dpi), 96) + status_height();
  RECT bounds{0, 0, MulDiv(static_cast<int>(viewport_size_.width), static_cast<int>(dpi), 96),
              client_height};
  AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)),
                           FALSE, static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)), dpi);
  RECT current{};
  GetWindowRect(window_, &current);
  MONITORINFO monitor{sizeof(MONITORINFO)};
  GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
  const int width =
      std::min(bounds.right - bounds.left, monitor.rcWork.right - monitor.rcWork.left);
  const int x = std::clamp(current.left, monitor.rcWork.left, monitor.rcWork.right - width);
  const auto position = [&](int height) {
    height = std::min<int>(height, monitor.rcWork.bottom - monitor.rcWork.top);
    const int y = std::clamp(current.top, monitor.rcWork.top, monitor.rcWork.bottom - height);
    // Small custom viewports can be narrower than the system's default tracking size.
    SetWindowPos(window_, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
  };
  const bool menu = GetMenu(window_) != nullptr;
  position(bounds.bottom - bounds.top + (menu ? GetSystemMetricsForDpi(SM_CYMENU, dpi) : 0));
  MENUBARINFO info{sizeof(MENUBARINFO)};
  if (menu && GetMenuBarInfo(window_, OBJID_MENU, 0, &info))
    position(bounds.bottom - bounds.top + info.rcBar.bottom - info.rcBar.top);
  RECT client{}, resized{};
  GetClientRect(window_, &client);
  GetWindowRect(window_, &resized);
  if (client.bottom > 0 && client.bottom != client_height &&
      resized.bottom - resized.top < monitor.rcWork.bottom - monitor.rcWork.top)
    position(resized.bottom - resized.top + client_height - client.bottom);
}

void Window::remember_window_size() {
  if (!window_ || fullscreen_ || IsZoomed(window_))
    return;
  RECT client{};
  GetClientRect(window_, &client);
  const auto dpi = static_cast<int>(GetDpiForWindow(window_));
  const auto width = MulDiv(client.right, 96, dpi);
  const auto height = MulDiv(std::max(0L, client.bottom - status_height()), 96, dpi);
  if (width >= 64 && width <= 8192 && height >= 64 && height <= 8192) {
    viewport_size_ = {static_cast<unsigned>(width), static_cast<unsigned>(height)};
    save_display_settings();
    update_display();
  }
}

void Window::position_fullscreen() {
  MONITORINFO monitor{sizeof(MONITORINFO)};
  GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
  auto bounds = monitor.rcMonitor;
  for (const auto &candidate : monitors_)
    if (candidate.device == fullscreen_monitor_)
      bounds = candidate.bounds;
  SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left,
               bounds.bottom - bounds.top, SWP_FRAMECHANGED);
}

void Window::fullscreen() {
  if (!fullscreen_) {
    remember_window_size();
    GetWindowPlacement(window_, &placement_);
    fullscreen_ = true;
    SetWindowLongPtrW(window_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetMenu(window_, nullptr);
    position_fullscreen();
  } else {
    fullscreen_ = false;
    SetWindowLongPtrW(window_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetMenu(window_, menu_);
    SetWindowPlacement(window_, &placement_);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (!IsZoomed(window_))
      apply_window_size();
  }
}

bool Window::display_command(unsigned id) {
  const bool preset =
      id >= ViewportPresetFirst && id < ViewportPresetFirst + viewport_presets.size();
  const bool monitor =
      id >= FullscreenMonitorFirst && id < FullscreenMonitorFirst + monitors_.size();
  if (!preset && !monitor && id != ViewportCustom && id != FullscreenMonitorCurrent &&
      !(id >= DisplayFit && id <= AspectPixels))
    return false;
  try {
    if (id >= DisplayFit && id <= DisplayStretch)
      display_scale_ = static_cast<DisplayScale>(id - DisplayFit);
    if (id == AspectStandard || id == AspectPixels)
      display_aspect_ = static_cast<DisplayAspect>(id - AspectStandard);
    if (monitor || id == FullscreenMonitorCurrent) {
      fullscreen_monitor_ = monitor ? monitors_[id - FullscreenMonitorFirst].device : L"";
      if (fullscreen_)
        position_fullscreen();
    }
    if (preset || id == ViewportCustom) {
      const auto size = preset ? std::optional(viewport_presets[id - ViewportPresetFirst])
                               : choose_viewport_size(window_, viewport_size_);
      if (!size)
        return true;
      if (fullscreen_)
        fullscreen();
      viewport_size_ = *size;
      apply_window_size();
    }
    save_display_settings();
  } catch (const std::exception &error) {
    status_error(L"Display setting failed", error);
  }
  update_display();
  rebase();
  return true;
}

} // namespace cupid::desktop
