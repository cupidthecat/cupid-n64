#include "desktop/windows/window.hpp"
#include "desktop/windows/commands.hpp"
#include <algorithm>
#include <commdlg.h>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>

namespace cupid::desktop {
namespace {
constexpr auto help = L"Move: W A S D (hold Shift to walk)\n"
                      L"A / jump: J     B / punch: K     Z / crouch: Space\n"
                      L"Start: Enter     L / R: Q / E\n"
                      L"C buttons / camera: Arrow keys\n"
                      L"D-pad: I / U / O / P (up / left / down / right)\n\n"
                      L"Pause: F5 or Escape     Frame advance while paused: F4\n"
                      L"Reset: F6\n"
                      L"Fast-forward: hold Tab or toggle F9\n"
                      L"Rewind: hold F7; enable and configure it in Emulation > Rewind.\n"
                      L"Save state: F2     Load state: F3\n"
                      L"Undo save: Ctrl+F2     Undo load: Ctrl+F3\n"
                      L"State slots: Ctrl+1 through Ctrl+9; Ctrl+0 selects slot 10.\n"
                      L"Mute: F8     Fullscreen: F11\n\n"
                      L"Screenshot of the displayed game image: F12\n"
                      L"View selects scaling, aspect, window size, and fullscreen monitor.\n"
                      L"Integer scaling uses whole output-height multiples, or best fit when "
                      L"the window is smaller than one frame.\n\n"
                      L"The game pauses when this window loses focus.\n"
                      L"Cartridge saves use .eep, .sra, .fla or .rtc; Controller Paks use .pak.\n"
                      L"Disk saves use .disk and .rtc.\n"
                      L"Save states use the game's .states folder. Undo lasts until another game "
                      L"is opened or the app closes.";
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
    if (options_.rom.empty() && options_.ipl.empty())
      options_.rom = setting(settings_, L"ROM");
    if (options_.firmware.empty())
      options_.firmware = setting(settings_, L"Firmware");
    state_slot_ = std::clamp(GetPrivateProfileIntW(L"States", L"Slot", 1, settings_.c_str()), 1u,
                             StateSlots::count);
    rewind_enabled_ = GetPrivateProfileIntW(L"Rewind", L"Enabled", 0, settings_.c_str()) != 0;
    rewind_mute_ = GetPrivateProfileIntW(L"Rewind", L"Mute", 1, settings_.c_str()) != 0;
    const auto choice = [&](const wchar_t *key, const auto &values, unsigned fallback) {
      const auto value = GetPrivateProfileIntW(L"Rewind", key, fallback, settings_.c_str());
      return std::find(values.begin(), values.end(), value) != values.end() ? value : fallback;
    };
    rewind_settings_.interval = choice(L"Interval", rewind_intervals, 60);
    rewind_settings_.capacity = choice(L"Capacity", rewind_capacities, 10);
    rewind_settings_.memory = std::size_t(choice(L"MemoryMB", rewind_memory, 256)) * 1024 * 1024;
  }
  load_display_settings();
  load_recent();
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
  recent_menu_ = CreatePopupMenu();
  AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(recent_menu_), L"&Recent ROMs");
  AppendMenuW(file, MF_STRING, Firmware, L"Select &PIF firmware...");
  AppendMenuW(file, MF_STRING | MF_GRAYED, Screenshot, L"&Screenshot...\tF12");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, Exit, L"E&xit");
  AppendMenuW(emulation, MF_STRING, Pause, L"&Pause\tF5");
  AppendMenuW(emulation, MF_STRING | MF_GRAYED, FrameAdvance, L"Frame &advance\tF4");
  AppendMenuW(emulation, MF_STRING, Reset, L"&Reset\tF6");
  AppendMenuW(emulation, MF_STRING, Mute, L"&Mute\tF8");
  AppendMenuW(emulation, MF_STRING, FastForward, L"&Fast-forward\tF9 / hold Tab");
  rewind_menu_ = CreatePopupMenu();
  AppendMenuW(rewind_menu_, MF_STRING, RewindEnabled, L"&Enable rewind");
  AppendMenuW(rewind_menu_, MF_STRING, RewindMute, L"&Mute while rewinding");
  AppendMenuW(rewind_menu_, MF_STRING | MF_GRAYED, 0, L"Hold F7 to rewind");
  const auto intervals = CreatePopupMenu(), capacities = CreatePopupMenu(),
             memory = CreatePopupMenu();
  for (unsigned n = 0; n < rewind_intervals.size(); ++n) {
    const auto label = L"Every " + std::to_wstring(rewind_intervals[n]) + L" video frames";
    AppendMenuW(intervals, MF_STRING, RewindIntervalFirst + n, label.c_str());
  }
  for (unsigned n = 0; n < rewind_capacities.size(); ++n) {
    const auto label = L"Up to " + std::to_wstring(rewind_capacities[n]) + L" snapshots";
    AppendMenuW(capacities, MF_STRING, RewindCapacityFirst + n, label.c_str());
  }
  for (unsigned n = 0; n < rewind_memory.size(); ++n) {
    const auto label = std::to_wstring(rewind_memory[n]) + L" MB";
    AppendMenuW(memory, MF_STRING, RewindMemoryFirst + n, label.c_str());
  }
  AppendMenuW(rewind_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(intervals),
              L"Snapshot &frequency");
  AppendMenuW(rewind_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(capacities), L"&History length");
  AppendMenuW(rewind_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(memory), L"Memory &limit");
  AppendMenuW(emulation, MF_POPUP, reinterpret_cast<UINT_PTR>(rewind_menu_), L"&Rewind");
  state_menu_ = CreatePopupMenu();
  AppendMenuW(state_menu_, MF_STRING, SaveState, L"&Save state\tF2");
  AppendMenuW(state_menu_, MF_STRING, LoadState, L"&Load state\tF3");
  AppendMenuW(state_menu_, MF_STRING, UndoStateSave, L"Undo s&ave\tCtrl+F2");
  AppendMenuW(state_menu_, MF_STRING, UndoStateLoad, L"Undo l&oad\tCtrl+F3");
  AppendMenuW(state_menu_, MF_SEPARATOR, 0, nullptr);
  for (unsigned slot = 1; slot <= StateSlots::count; ++slot)
    AppendMenuW(state_menu_, MF_STRING, StateSlotFirst + slot - 1, L"Slot");
  AppendMenuW(view, MF_STRING, Fullscreen, L"&Fullscreen\tF11");
  create_display_menu(view);
  AppendMenuW(view, MF_STRING, Controls, L"&Keyboard controls\tF1");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation), L"&Emulation");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(state_menu_), L"&States");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
  RECT bounds{0, 0, static_cast<LONG>(viewport_size_.width),
              static_cast<LONG>(viewport_size_.height + 24)};
  AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, TRUE);
  window_ = CreateWindowW(type.lpszClassName, L"Cupid-N64", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                          nullptr, menu_, instance, this);
  if (!window_)
    throw std::runtime_error("Could not create the desktop window.");
  apply_window_size();
  DragAcceptFiles(window_, TRUE);
  update_recent();
  update_states();
  update_playback();
  update_display();
  ShowWindow(window_, show);
  UpdateWindow(window_);
  try {
    if (options_.frames &&
        ((options_.rom.empty() && options_.ipl.empty()) || options_.firmware.empty()))
      throw std::runtime_error("A frame test requires --rom or --ipl, plus --pif.");
    if ((!options_.rom.empty() || !options_.ipl.empty()) && !options_.firmware.empty())
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
    try {
      if (rewinding_) {
        if (std::chrono::steady_clock::now() >= next_rewind_)
          rewind_step();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
        continue;
      }
      if (frame_ready_) {
        const auto delay =
            playback_clock_.delay(session_->clocks(), std::chrono::steady_clock::now());
        if (!fast_forward_ && delay.count()) {
          MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(delay.count()),
                                    QS_ALLINPUT);
          continue;
        }
        present();
      } else {
        advance();
      }
    } catch (const std::exception &exception) {
      error(exception);
    }
  }
  end_rewind();
  stop_fast_forward();
  if (session_) {
    try {
      session_->save();
    } catch (const std::exception &exception) {
      error(exception);
    }
  }
  rewind_.reset();
  states_.reset();
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
    end_rewind();
    stop_fast_forward();
    if (session_)
      session_->save();
    quit_ = true;
    return 0;
  case WM_COMMAND:
    command(LOWORD(wparam));
    return 0;
  case WM_DROPFILES:
    drop_rom(wparam);
    return 0;
  case WM_INITMENUPOPUP:
    if (reinterpret_cast<HMENU>(wparam) == state_menu_)
      update_states();
    if (reinterpret_cast<HMENU>(wparam) == monitor_menu_)
      refresh_monitors();
    if (reinterpret_cast<HMENU>(wparam) == recent_menu_)
      update_recent();
    return 0;
  case WM_ACTIVATE:
    active_ = LOWORD(wparam) != WA_INACTIVE || options_.frames != 0;
    end_rewind();
    stop_fast_forward();
    keys_.fill(false);
    audio_.clear();
    rebase();
    return 0;
  case WM_ENTERMENULOOP:
  case WM_ENTERSIZEMOVE:
    modal_ = true;
    end_rewind();
    stop_fast_forward();
    keys_.fill(false);
    audio_.clear();
    return 0;
  case WM_EXITMENULOOP:
  case WM_EXITSIZEMOVE:
    modal_ = false;
    update_fast_forward();
    if (event == WM_EXITSIZEMOVE)
      remember_window_size();
    rebase();
    return 0;
  case WM_KEYDOWN:
    if (wparam < keys_.size())
      keys_[wparam] = true;
    if (!(lparam & (1L << 30))) {
      const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (wparam == VK_F2)
        command(control ? UndoStateSave : SaveState);
      if (wparam == VK_F3)
        command(control ? UndoStateLoad : LoadState);
      if (wparam == VK_F4)
        command(FrameAdvance);
      if (control && wparam >= '0' && wparam <= '9') {
        const auto slot = wparam == '0' ? StateSlots::count : static_cast<unsigned>(wparam - '0');
        command(StateSlotFirst + slot - 1);
      }
      if (wparam == VK_F5 || wparam == VK_ESCAPE)
        command(Pause);
      if (wparam == VK_F6)
        command(Reset);
      if (wparam == VK_F7)
        command(BeginRewind);
      if (wparam == VK_F8)
        command(Mute);
      if (wparam == VK_F9)
        command(FastForward);
      if (wparam == VK_TAB)
        command(HoldFastForward);
      if (wparam == VK_F11)
        command(Fullscreen);
      if (wparam == VK_F12)
        command(Screenshot);
      if (wparam == VK_F1)
        command(Controls);
    }
    return 0;
  case WM_KEYUP:
    if (wparam < keys_.size())
      keys_[wparam] = false;
    if (wparam == VK_F7)
      command(EndRewind);
    if (wparam == VK_TAB)
      command(ReleaseFastForward);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  case WM_SIZE:
    InvalidateRect(window_, nullptr, FALSE);
    return 0;
  case WM_DPICHANGED:
    if (fullscreen_) {
      position_fullscreen();
    } else {
      const auto &bounds = *reinterpret_cast<const RECT *>(lparam);
      SetWindowPos(window_, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
      apply_window_size();
    }
    return 0;
  case WM_DISPLAYCHANGE:
    refresh_monitors();
    if (fullscreen_)
      position_fullscreen();
    else
      apply_window_size();
    return 0;
  default:
    return DefWindowProcW(window_, event, wparam, lparam);
  }
}

void Window::error(const std::exception &exception) {
  end_rewind();
  stop_fast_forward();
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
  if ((options_.rom.empty() && options_.ipl.empty()) || options_.firmware.empty())
    return;
  end_rewind();
  stop_fast_forward();
  if (session_)
    session_->save();
  audio_.clear();
  auto next = std::make_unique<Session>(
      options_.rom, options_.firmware, audio_, options_.ipl, options_.disk, options_.arcade_profile,
      options_.frames ? std::optional<std::uint64_t>(0) : std::nullopt);
  auto states = std::make_unique<StateSlots>(
      options_.rom.empty() ? (options_.disk.empty() ? options_.ipl : options_.disk) : options_.rom,
      [this] { return session_->capture_state(); },
      [this](std::span<const std::uint8_t> bytes) { session_->restore_state(bytes); });
  states->select(state_slot_);
  auto rewind = std::make_unique<RewindHistory>(
      [this] { return session_->capture_state(); },
      [this](std::span<const std::uint8_t> bytes) { session_->restore_state(bytes); },
      rewind_settings_);
  rewind_ = std::move(rewind);
  states_ = std::move(states);
  session_ = std::move(next);
  state_message_.clear();
  game_image_ =
      options_.rom.empty() ? (options_.disk.empty() ? options_.ipl : options_.disk) : options_.rom;
  pixels_.clear();
  pixel_width_ = pixel_height_ = 0;
  frame_ready_ = false;
  last_frame_ = 0;
  paused_ = false;
  keys_.fill(false);
  rebase();
  update_states();
  update_playback();
  update_display();
  if (!settings_.empty() && !options_.frames) {
    if (!options_.rom.empty())
      WritePrivateProfileStringW(L"Files", L"ROM", options_.rom.c_str(), settings_.c_str());
    WritePrivateProfileStringW(L"Files", L"Firmware", options_.firmware.c_str(), settings_.c_str());
  }
  if (!options_.rom.empty()) {
    recent_.remember(options_.rom);
    try {
      save_recent();
    } catch (const std::exception &error) {
      status_error(L"Recent ROMs could not be saved", error);
    }
    update_recent();
  }
}

std::filesystem::path Window::choose(bool firmware) {
  std::array<wchar_t, 32768> path{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrFile = path.data();
  dialog.nMaxFile = static_cast<DWORD>(path.size());
  dialog.lpstrTitle = firmware ? (options_.arcade_profile == n64::ArcadeProfile::Disabled
                                      ? L"Select PIF firmware (1984 bytes)"
                                      : L"Select Aleck64 PIF firmware (1984 or 2048 bytes)")
                               : L"Open Nintendo 64 ROM";
  dialog.lpstrFilter = firmware ? L"PIF firmware\0*.bin;*.rom\0All files\0*.*\0"
                                : L"N64 cartridge\0*.z64;*.n64;*.v64\0All files\0*.*\0";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&dialog) ? std::filesystem::path(path.data()) : std::filesystem::path{};
}

void Window::command(unsigned id) {
  if (file_command(id))
    return;
  if (display_command(id))
    return;
  if (playback_command(id))
    return;
  if (state_command(id))
    return;
  if (id == Screenshot) {
    screenshot();
  } else if (id == Firmware) {
    auto path = choose(true);
    if (path.empty())
      return;
    options_.firmware = path;
    load();
  } else if (id == Exit) {
    SendMessageW(window_, WM_CLOSE, 0, 0);
  } else if (id == Pause) {
    end_rewind();
    stop_fast_forward();
    paused_ = !paused_;
    audio_.clear();
    if (paused_ && session_) {
      session_->save();
      session_->finish_frame();
      frame_ready_ = false;
      last_frame_ = session_->frames;
      refresh_frame();
    }
    rebase();
  } else if (id == Reset) {
    end_rewind();
    stop_fast_forward();
    if (rewind_)
      rewind_->clear();
    if (session_) {
      session_->save();
      session_->reset();
      pixels_.clear();
      pixel_width_ = pixel_height_ = 0;
      update_display();
      frame_ready_ = false;
      last_frame_ = 0;
      paused_ = false;
      keys_.fill(false);
      rebase();
    }
  } else if (id == FrameAdvance && session_ && paused_) {
    const auto control = input();
    session_->advance_frame(control.buttons, control.x, control.y);
    frame_ready_ = false;
    last_frame_ = session_->frames;
    refresh_frame();
    record_history();
    rebase();
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
  update_playback();
}

void Window::paint() {
  PAINTSTRUCT painting{};
  const auto target = BeginPaint(window_, &painting);
  RECT client{};
  GetClientRect(window_, &client);
  if (client.right <= 0 || client.bottom <= 0) {
    EndPaint(window_, &painting);
    return;
  }
  try {
    const auto dc = paint_buffer_.begin(target, client.right, client.bottom);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const int width = client.right, height = std::max(0L, client.bottom - status_height());
    if (session_ && !pixels_.empty()) {
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = static_cast<LONG>(pixel_width_);
      info.bmiHeader.biHeight = -static_cast<LONG>(pixel_height_);
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      const auto viewport =
          image_viewport(pixel_width_, pixel_height_, static_cast<unsigned>(width),
                         static_cast<unsigned>(height), display_scale_, display_aspect_);
      SetStretchBltMode(dc, COLORONCOLOR);
      StretchDIBits(dc, static_cast<int>(viewport.x), static_cast<int>(viewport.y),
                    static_cast<int>(viewport.width), static_cast<int>(viewport.height), 0, 0,
                    static_cast<int>(pixel_width_), static_cast<int>(pixel_height_), pixels_.data(),
                    &info, DIB_RGB_COLORS, SRCCOPY);
    } else {
      SetTextColor(dc, RGB(220, 220, 220));
      SetBkMode(dc, TRANSPARENT);
      RECT content{20, 20, width - 20, height - 20};
      DrawTextW(dc,
                L"CUPID-N64\n\nFile > Open ROM to load a Nintendo 64 game.\n"
                L"Select the game's PIF firmware when prompted.\n\n"
                L"Press F1 for keyboard controls.",
                -1, &content, DT_CENTER);
    }
    RECT bar{0, height, width, client.bottom};
    FillRect(dc, &bar, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    bar.left = 8;
    std::wstring status =
        stopped()
            ? L"Paused  |  F5 Resume  |  F1 Controls  |  F11 Fullscreen"
            : L"WASD Move  |  J Jump  |  K Punch  |  Enter Start  |  F1 Controls  |  F5 Pause";
    if (states_)
      status = L"Slot " + std::to_wstring(state_slot_) + L"  |  F2 Save  |  F3 Load  |  " + status;
    if (!state_message_.empty())
      status = state_message_ + L"  |  " + status;
    DrawTextW(dc, status.c_str(), -1, &bar, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    paint_buffer_.present(target);
  } catch (...) {
    EndPaint(window_, &painting);
    throw;
  }
  EndPaint(window_, &painting);
}

Window::Input Window::input() const {
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
  return {buttons, x, y};
}

void Window::advance() {
  const auto control = input();
  const auto interval_start = session_->clocks();
  do {
    session_->run(control.buttons, control.x, control.y);
  } while (session_->frames == last_frame_ && session_->clocks() - interval_start < 1875000);
  if (session_->frames == last_frame_)
    return;
  frame_ready_ = true;
}

void Window::present() {
  // The VI callback takes the prior readback; let the new one overlap emulation.
  frame_ready_ = false;
  last_frame_ = session_->frames;
  refresh_frame();
  record_history();
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
    const auto title = L"Cupid-N64 | " + game_image_.filename().wstring() + L" | " +
                       std::to_wstring(static_cast<unsigned>(fps + 0.5)) + L" VI/s" + L" | Slot " +
                       std::to_wstring(state_slot_) +
                       (audio_.available() ? L"" : L" | Audio unavailable");
    SetWindowTextW(window_, title.c_str());
    measured_time_ = now;
    measured_frames_ = last_frame_;
  }
}

void Window::capture() {
  session_->finish_frame();
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
