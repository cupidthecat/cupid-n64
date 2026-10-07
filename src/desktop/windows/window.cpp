#include "desktop/windows/window.hpp"
#include <algorithm>
#include <commdlg.h>
#include <fstream>
#include <stdexcept>

namespace cupid::desktop {
namespace {
enum Command : unsigned { Open = 1, Firmware, Exit, Pause, Reset, Fullscreen, Mute, Controls };
constexpr auto help = L"Move: W A S D (hold Shift to walk)\n"
                      L"A / jump: J     B / punch: K     Z / crouch: Space\n"
                      L"Start: Enter     L / R: Q / E\n"
                      L"C buttons / camera: Arrow keys\n"
                      L"D-pad: I / U / O / P (up / left / down / right)\n\n"
                      L"Pause: F5 or Escape     Reset: F6\n"
                      L"Mute: F8     Fullscreen: F11\n\n"
                      L"The game pauses when this window loses focus.\n"
                      L"In-game saves are stored as .eep beside your ROM.\n"
                      L"This frontend uses the NTSC SM64 cartridge profile.";
std::filesystem::path setting(const std::filesystem::path &file, const wchar_t *name) {
  std::array<wchar_t, 32768> buffer{};
  GetPrivateProfileStringW(L"Files", name, L"", buffer.data(), static_cast<DWORD>(buffer.size()),
                           file.c_str());
  return buffer.data();
}
} // namespace

Window::Window(Options options) : options_(std::move(options)) {
  std::array<wchar_t, 32768> directory{};
  const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(),
                                            static_cast<DWORD>(directory.size()));
  if (size && size < directory.size()) {
    settings_ = std::filesystem::path(directory.data()) / L"Cupid-N64";
    std::filesystem::create_directories(settings_);
    settings_ /= L"settings.ini";
    if (options_.rom.empty())
      options_.rom = setting(settings_, L"ROM");
    if (options_.firmware.empty())
      options_.firmware = setting(settings_, L"Firmware");
  }
}

int Window::run(HINSTANCE instance, int show) {
  WNDCLASSW type{};
  type.lpfnWndProc = procedure;
  type.hInstance = instance;
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  type.lpszClassName = L"CupidN64Desktop";
  if (!RegisterClassW(&type))
    throw std::runtime_error("Could not register the desktop window.");
  menu_ = CreateMenu();
  const auto file = CreatePopupMenu(), emulation = CreatePopupMenu(), view = CreatePopupMenu();
  AppendMenuW(file, MF_STRING, Open, L"&Open ROM...");
  AppendMenuW(file, MF_STRING, Firmware, L"Select &PIF firmware...");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, Exit, L"E&xit");
  AppendMenuW(emulation, MF_STRING, Pause, L"&Pause\tF5");
  AppendMenuW(emulation, MF_STRING, Reset, L"&Reset\tF6");
  AppendMenuW(emulation, MF_STRING, Mute, L"&Mute\tF8");
  AppendMenuW(view, MF_STRING, Fullscreen, L"&Fullscreen\tF11");
  AppendMenuW(view, MF_STRING, Controls, L"&Keyboard controls\tF1");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation), L"&Emulation");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
  RECT bounds{0, 0, 960, 744};
  AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, TRUE);
  window_ = CreateWindowW(type.lpszClassName, L"Cupid-N64", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                          nullptr, menu_, instance, this);
  if (!window_)
    throw std::runtime_error("Could not create the desktop window.");
  ShowWindow(window_, show);
  UpdateWindow(window_);
  try {
    if (options_.frames && (options_.rom.empty() || options_.firmware.empty()))
      throw std::runtime_error("A frame test requires both --rom and --pif.");
    if (!options_.rom.empty() && !options_.firmware.empty())
      load();
  } catch (const std::exception &exception) {
    error(exception);
  }
  while (!quit_) {
    MSG pending{};
    while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
      if (pending.message == WM_QUIT) {
        quit_ = true;
        break;
      }
      TranslateMessage(&pending);
      DispatchMessageW(&pending);
    }
    if (quit_)
      break;
    if (!session_ || stopped()) {
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
      continue;
    }
    const auto delay = playback_clock_.delay(session_->clocks(), std::chrono::steady_clock::now());
    if (delay.count()) {
      MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(delay.count()), QS_ALLINPUT);
      continue;
    }
    try {
      advance();
    } catch (const std::exception &exception) {
      error(exception);
    }
  }
  if (session_) {
    try {
      session_->save();
    } catch (const std::exception &exception) {
      error(exception);
    }
  }
  session_.reset();
  if (IsWindow(window_))
    DestroyWindow(window_);
  return exit_code_;
}

LRESULT CALLBACK Window::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto self = reinterpret_cast<Window *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self)
    return DefWindowProcW(window, message, wparam, lparam);
  try {
    return self->message(message, wparam, lparam);
  } catch (const std::exception &exception) {
    self->error(exception);
    return 0;
  }
}

LRESULT Window::message(UINT event, WPARAM wparam, LPARAM lparam) {
  switch (event) {
  case WM_CLOSE:
    if (session_)
      session_->save();
    quit_ = true;
    return 0;
  case WM_COMMAND:
    command(LOWORD(wparam));
    return 0;
  case WM_ACTIVATE:
    active_ = LOWORD(wparam) != WA_INACTIVE || options_.frames != 0;
    keys_.fill(false);
    audio_.clear();
    rebase();
    return 0;
  case WM_ENTERMENULOOP:
  case WM_ENTERSIZEMOVE:
    modal_ = true;
    keys_.fill(false);
    audio_.clear();
    return 0;
  case WM_EXITMENULOOP:
  case WM_EXITSIZEMOVE:
    modal_ = false;
    rebase();
    return 0;
  case WM_KEYDOWN:
    if (wparam < keys_.size())
      keys_[wparam] = true;
    if (!(lparam & (1L << 30))) {
      if (wparam == VK_F5 || wparam == VK_ESCAPE)
        command(Pause);
      if (wparam == VK_F6)
        command(Reset);
      if (wparam == VK_F8)
        command(Mute);
      if (wparam == VK_F11)
        command(Fullscreen);
      if (wparam == VK_F1)
        command(Controls);
    }
    return 0;
  case WM_KEYUP:
    if (wparam < keys_.size())
      keys_[wparam] = false;
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  case WM_SIZE:
    InvalidateRect(window_, nullptr, FALSE);
    return 0;
  default:
    return DefWindowProcW(window_, event, wparam, lparam);
  }
}

void Window::error(const std::exception &exception) {
  paused_ = true;
  audio_.clear();
  if (options_.frames) {
    if (!options_.capture.empty()) {
      auto log = options_.capture;
      log += L".error.txt";
      std::ofstream(log) << exception.what() << '\n';
    }
    exit_code_ = 1;
    quit_ = true;
    return;
  }
  MessageBoxA(window_, exception.what(), "Cupid-N64", MB_OK | MB_ICONERROR);
}

void Window::rebase() {
  measured_time_ = std::chrono::steady_clock::now();
  playback_clock_.reset(session_ ? session_->clocks() : 0, measured_time_);
  measured_frames_ = session_ ? session_->frames : 0;
  InvalidateRect(window_, nullptr, FALSE);
}

void Window::load() {
  if (options_.rom.empty() || options_.firmware.empty())
    return;
  if (session_)
    session_->save();
  audio_.clear();
  auto next = std::make_unique<Session>(options_.rom, options_.firmware, audio_);
  session_ = std::move(next);
  pixels_.clear();
  last_frame_ = 0;
  paused_ = false;
  keys_.fill(false);
  rebase();
  if (!settings_.empty() && !options_.frames) {
    WritePrivateProfileStringW(L"Files", L"ROM", options_.rom.c_str(), settings_.c_str());
    WritePrivateProfileStringW(L"Files", L"Firmware", options_.firmware.c_str(), settings_.c_str());
  }
}

std::filesystem::path Window::choose(bool firmware) {
  std::array<wchar_t, 32768> path{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrFile = path.data();
  dialog.nMaxFile = static_cast<DWORD>(path.size());
  dialog.lpstrTitle = firmware ? L"Select NTSC PIF firmware (1984 bytes)" : L"Open SM64 ROM";
  dialog.lpstrFilter = firmware ? L"PIF firmware\0*.bin;*.rom\0All files\0*.*\0"
                                : L"N64 cartridge\0*.z64;*.n64;*.v64\0All files\0*.*\0";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&dialog) ? std::filesystem::path(path.data()) : std::filesystem::path{};
}

void Window::command(unsigned id) {
  if (id == Open || id == Firmware) {
    auto path = choose(id == Firmware);
    if (path.empty())
      return;
    if (id == Open)
      options_.rom = path;
    else
      options_.firmware = path;
    if (options_.firmware.empty())
      options_.firmware = choose(true);
    load();
  } else if (id == Exit) {
    SendMessageW(window_, WM_CLOSE, 0, 0);
  } else if (id == Pause) {
    paused_ = !paused_;
    audio_.clear();
    if (paused_ && session_)
      session_->save();
    rebase();
  } else if (id == Reset) {
    load();
  } else if (id == Mute) {
    audio_.muted = !audio_.muted;
    audio_.clear();
  } else if (id == Fullscreen) {
    fullscreen();
  } else if (id == Controls) {
    std::wstring text = help;
    if (!audio_.available())
      text += L"\n\nAudio: " + audio_.error();
    MessageBoxW(window_, text.c_str(), L"Cupid-N64 controls", MB_OK);
    rebase();
  }
  CheckMenuItem(menu_, Pause, MF_BYCOMMAND | (paused_ ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(menu_, Mute, MF_BYCOMMAND | (audio_.muted ? MF_CHECKED : MF_UNCHECKED));
}

void Window::fullscreen() {
  fullscreen_ = !fullscreen_;
  if (fullscreen_) {
    GetWindowPlacement(window_, &placement_);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
    SetWindowLongPtrW(window_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetMenu(window_, nullptr);
    SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                 monitor.rcMonitor.right - monitor.rcMonitor.left,
                 monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
  } else {
    SetWindowLongPtrW(window_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetMenu(window_, menu_);
    SetWindowPlacement(window_, &placement_);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
}

void Window::paint() {
  PAINTSTRUCT painting{};
  const auto dc = BeginPaint(window_, &painting);
  RECT client{};
  GetClientRect(window_, &client);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  const int width = client.right, height = std::max(0L, client.bottom - 24);
  if (session_ && !pixels_.empty()) {
    const auto &frame = session_->frame;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(frame.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int scaled_width = std::min(width, height * 4 / 3);
    const int scaled_height = scaled_width * 3 / 4;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, (width - scaled_width) / 2, (height - scaled_height) / 2, scaled_width,
                  scaled_height, 0, 0, static_cast<int>(frame.width),
                  static_cast<int>(frame.height), pixels_.data(), &info, DIB_RGB_COLORS, SRCCOPY);
  } else {
    SetTextColor(dc, RGB(220, 220, 220));
    SetBkMode(dc, TRANSPARENT);
    RECT content{20, 20, width - 20, height - 20};
    DrawTextW(dc,
              L"CUPID-N64\n\nFile > Open ROM to load Super Mario 64.\n"
              L"Select your NTSC PIF firmware when prompted.\n\n"
              L"Press F1 for keyboard controls.",
              -1, &content, DT_CENTER);
  }
  RECT bar{0, height, width, client.bottom};
  FillRect(dc, &bar, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
  SetTextColor(dc, RGB(255, 255, 255));
  SetBkMode(dc, TRANSPARENT);
  bar.left = 8;
  const auto status =
      stopped() ? L"Paused  |  F5 Resume  |  F1 Controls  |  F11 Fullscreen"
                : L"WASD Move  |  J Jump  |  K Punch  |  Enter Start  |  F1 Controls  |  F5 Pause";
  DrawTextW(dc, status, -1, &bar, DT_SINGLELINE | DT_VCENTER);
  EndPaint(window_, &painting);
}

void Window::advance() {
  std::uint16_t buttons = 0;
  constexpr std::array<unsigned, 14> codes{'J',   'K',     VK_SPACE, VK_RETURN, 'I',
                                           'O',   'U',     'P',      'Q',       'E',
                                           VK_UP, VK_DOWN, VK_LEFT,  VK_RIGHT};
  constexpr std::array<std::uint16_t, 14> masks{0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200,
                                                0x100,  0x20,   0x10,   8,      4,     2,     1};
  for (unsigned n = 0; n < codes.size(); ++n)
    if (keys_[codes[n]])
      buttons |= masks[n];
  const int magnitude = keys_[VK_SHIFT] ? 35 : 80;
  auto x = static_cast<std::int8_t>((int(keys_['D']) - int(keys_['A'])) * magnitude);
  auto y = static_cast<std::int8_t>((int(keys_['W']) - int(keys_['S'])) * magnitude);
  if (options_.test_input) {
    const unsigned frame = session_->frames;
    const bool start = (frame >= 900 && frame < 960) || (frame >= 1200 && frame < 1260);
    const bool jump = (frame >= 1600 && frame < 1660) || (frame >= 2200 && frame < 2260) ||
                      (frame >= 2600 && frame < 2660) || (frame >= 3000 && frame < 3060) ||
                      (frame >= 3200 && frame % 300 < 60);
    buttons = static_cast<std::uint16_t>((start ? 0x1000 : 0) | (jump ? 0x8000 : 0));
    x = 0;
    y = frame >= 4800 ? -58 : 0;
  }
  const auto interval_start = session_->clocks();
  do {
    session_->run(buttons, x, y);
  } while (session_->frames == last_frame_ && session_->clocks() - interval_start < 1875000);
  if (session_->frames == last_frame_)
    return;
  last_frame_ = session_->frames;
  pixels_ = session_->frame.rgba;
  for (std::size_t n = 0; n < pixels_.size(); n += 4)
    std::swap(pixels_[n], pixels_[n + 2]);
  InvalidateRect(window_, nullptr, FALSE);
  if (last_frame_ % 300 == 0)
    session_->save();
  if (options_.frames && last_frame_ >= options_.frames) {
    capture();
    quit_ = true;
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(now - measured_time_).count();
  if (elapsed >= 1.0) {
    const auto fps = (last_frame_ - measured_frames_) / elapsed;
    const auto title = L"Cupid-N64 | " + options_.rom.filename().wstring() + L" | " +
                       std::to_wstring(static_cast<unsigned>(fps + 0.5)) + L" VI/s" +
                       (audio_.available() ? L"" : L" | Audio unavailable");
    SetWindowTextW(window_, title.c_str());
    measured_time_ = now;
    measured_frames_ = last_frame_;
  }
}

void Window::capture() {
  if (session_->frame.rgba.empty())
    throw std::runtime_error("No video frame was produced.");
  if (options_.capture.empty())
    return;
  const auto &frame = session_->frame;
  std::ofstream stream(options_.capture, std::ios::binary);
  stream << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  for (std::size_t n = 0; n < frame.rgba.size(); n += 4)
    stream.write(reinterpret_cast<const char *>(frame.rgba.data() + n), 3);
  stream.close();
  if (!stream)
    throw std::runtime_error("Could not save the captured frame.");
  auto report = options_.capture;
  report += L".txt";
  std::wofstream(report) << L"Frames: " << session_->frames << L"\nAudio: "
                         << (audio_.available() ? L"Ready" : audio_.error()) << L'\n';
}

} // namespace cupid::desktop
