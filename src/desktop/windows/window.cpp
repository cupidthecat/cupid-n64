#include "desktop/windows/window.hpp"
#include "core/controller/stick/response.hpp"
#include "desktop/windows/commands.hpp"
#include "desktop/windows/controls/keys.hpp"
#include <algorithm>
#include <commdlg.h>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>

namespace cupid::desktop {
namespace {
void record_key(std::array<bool, 256> &keys, WPARAM key, LPARAM message, bool pressed) {
  if (key >= keys.size())
    return;
  keys[key] = pressed;
  unsigned specific = 0, left = 0, right = 0;
  if (key == VK_SHIFT) {
    specific = MapVirtualKeyW(static_cast<UINT>((message >> 16) & 255), MAPVK_VSC_TO_VK_EX);
    left = VK_LSHIFT;
    right = VK_RSHIFT;
  } else if (key == VK_CONTROL || key == VK_MENU) {
    left = key == VK_CONTROL ? VK_LCONTROL : VK_LMENU;
    right = key == VK_CONTROL ? VK_RCONTROL : VK_RMENU;
    specific = message & (1L << 24) ? right : left;
  }
  if (specific && specific < keys.size()) {
    keys[specific] = pressed;
    keys[key] = keys[left] || keys[right];
  }
}

constexpr auto help = L"Default keyboard layout:\n"
                      L"Move: W A S D (hold Shift to walk)\n"
                      L"A / jump: J     B / punch: K     Z / crouch: Space\n"
                      L"Start: Enter     L / R: Q / E\n"
                      L"C buttons / camera: Arrow keys\n"
                      L"D-pad: I / U / O / P (up / left / down / right)\n\n"
                      L"View selects scaling, aspect, window size, and fullscreen monitor.\n"
                      L"Integer scaling uses whole output-height multiples, or best fit when "
                      L"the window is smaller than one frame.\n\n"
                      L"Emulation selects audio, playback synchronization, and focus behavior.\n"
                      L"Cartridge saves use .eep, .sra, .fla or .rtc; Controller Paks use .pak.\n"
                      L"Disk saves use .disk and .rtc.\n"
                      L"Save states use the game's .states folder. Undo lasts until another game "
                      L"is opened or the app closes.";
} // namespace

Window::Window(Options options) : options_(std::move(options)) {
  std::array<wchar_t, 32768> directory{};
  const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", directory.data(),
                                            static_cast<DWORD>(directory.size()));
  if (size && size < directory.size()) {
    settings_ = std::filesystem::path(directory.data()) / L"Cupid-N64";
    std::filesystem::create_directories(settings_);
    settings_ /= L"settings.ini";
  }
  load_history_settings();
  load_display_settings();
  load_graphics_settings();
  shader_.select(read_shader_selection(settings_));
  load_recent();
  load_playback_settings();
  load_launch_settings();
  player_mappings_ = read_mappings(settings_);
  shortcut_bindings_ = read_shortcuts(settings_);
  shortcuts_.bindings(shortcut_bindings_);
}

void Window::load_history_settings() {
  if (!settings_.empty()) {
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
  create_launch_menus(file, emulation);
  AppendMenuW(file, MF_STRING | MF_GRAYED, Screenshot, L"&Screenshot...");
  const auto settings_menu = CreatePopupMenu();
  AppendMenuW(settings_menu, MF_STRING, ImportSettings, L"&Import settings...");
  AppendMenuW(settings_menu, MF_STRING, ExportSettings, L"&Export settings...");
  AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(settings_menu), L"&Desktop settings");
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, Exit, L"E&xit");
  AppendMenuW(emulation, MF_STRING, Pause, L"&Pause");
  AppendMenuW(emulation, MF_STRING | MF_GRAYED, FrameAdvance, L"Frame &advance");
  AppendMenuW(emulation, MF_STRING, Reset, L"&Reset");
  AppendMenuW(emulation, MF_STRING, Mute, L"&Mute");
  AppendMenuW(emulation, MF_STRING, FastForward, L"&Fast-forward");
  rewind_menu_ = CreatePopupMenu();
  AppendMenuW(rewind_menu_, MF_STRING, RewindEnabled, L"&Enable rewind");
  AppendMenuW(rewind_menu_, MF_STRING, RewindMute, L"&Mute while rewinding");
  AppendMenuW(rewind_menu_, MF_STRING | MF_GRAYED, RewindHint, L"Rewind shortcut");
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
  create_playback_settings_menu(emulation);
  state_menu_ = CreatePopupMenu();
  AppendMenuW(state_menu_, MF_STRING, SaveState, L"&Save state");
  AppendMenuW(state_menu_, MF_STRING, LoadState, L"&Load state");
  AppendMenuW(state_menu_, MF_STRING, UndoStateSave, L"Undo s&ave");
  AppendMenuW(state_menu_, MF_STRING, UndoStateLoad, L"Undo l&oad");
  AppendMenuW(state_menu_, MF_SEPARATOR, 0, nullptr);
  for (unsigned slot = 1; slot <= StateSlots::count; ++slot)
    AppendMenuW(state_menu_, MF_STRING, StateSlotFirst + slot - 1, L"Slot");
  AppendMenuW(view, MF_STRING, Fullscreen, L"&Fullscreen");
  create_display_menu(view);
  create_graphics_menu(view);
  create_shader_menu(view);
  create_controller_menus(emulation, view);
  AppendMenuW(view, MF_STRING, Controls, L"&Controls / help");
  const auto developer = CreatePopupMenu();
  AppendMenuW(developer, MF_STRING, MemoryInspection, L"&Memory inspection...");
  AppendMenuW(developer, MF_STRING, DeviceProperties, L"Device &properties...");
  AppendMenuW(developer, MF_STRING, Disassembly, L"&Disassembly...");
  AppendMenuW(developer, MF_STRING, InstructionTrace, L"Instruction &trace...");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation), L"&Emulation");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(state_menu_), L"&States");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(developer), L"&Developer");
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
  update_launch();
  update_shortcuts();
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
        if (std::chrono::steady_clock::now() >= next_audio_check_) {
          refresh_audio_devices();
          next_audio_check_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }
        const auto delay =
            playback_clock_.delay(session_->clocks(), std::chrono::steady_clock::now());
        if (!fast_forward_ && delay.count()) {
          if (wait_for_video())
            continue;
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
    clear_host_input();
    gamepads_.stop();
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
    if (reinterpret_cast<HMENU>(wparam) == audio_device_menu_)
      refresh_audio_devices();
    return 0;
  case WM_DEVICECHANGE:
    gamepads_.stop();
    refresh_audio_devices();
    return 0;
  case WM_ACTIVATE: {
    const bool was_stopped = stopped();
    active_ = LOWORD(wparam) != WA_INACTIVE || options_.frames != 0;
    if (stopped())
      gamepads_.stop();
    end_rewind();
    stop_fast_forward();
    clear_host_input();
    if (was_stopped != stopped()) {
      audio_.clear();
      rebase();
    }
    return 0;
  }
  case WM_ENTERMENULOOP:
  case WM_ENTERSIZEMOVE:
    modal_ = true;
    gamepads_.stop();
    end_rewind();
    stop_fast_forward();
    clear_host_input();
    if (event == WM_ENTERSIZEMOVE)
      audio_.clear();
    return 0;
  case WM_EXITMENULOOP:
  case WM_EXITSIZEMOVE:
    modal_ = false;
    if (mouse_capture_pending_) {
      mouse_capture_pending_ = false;
      mouse_capture();
    }
    update_fast_forward();
    if (event == WM_EXITSIZEMOVE)
      remember_window_size();
    rebase();
    return 0;
  case WM_SYSKEYDOWN:
  case WM_KEYDOWN: {
    record_key(keys_, wparam, lparam, true);
    const bool handled =
        shortcut_key(static_cast<unsigned>(wparam), true, (lparam & (1L << 30)) != 0);
    return event == WM_SYSKEYDOWN && !handled ? DefWindowProcW(window_, event, wparam, lparam) : 0;
  }
  case WM_SYSKEYUP:
  case WM_KEYUP: {
    record_key(keys_, wparam, lparam, false);
    const bool handled = shortcut_key(static_cast<unsigned>(wparam), false, false);
    return event == WM_SYSKEYUP && !handled ? DefWindowProcW(window_, event, wparam, lparam) : 0;
  }
  case WM_INPUT:
    if (active_ && !stopped() && !rewinding_)
      mouse_.raw_input(reinterpret_cast<HRAWINPUT>(lparam));
    return DefWindowProcW(window_, event, wparam, lparam);
  case WM_LBUTTONDOWN: {
    RECT client{};
    GetClientRect(window_, &client);
    client.bottom -= status_height();
    POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
    if (has_mouse() && PtInRect(&client, point))
      mouse_capture();
    return 0;
  }
  case WM_CAPTURECHANGED:
    if (mouse_.captured() && reinterpret_cast<HWND>(lparam) != window_)
      release_mouse();
    return 0;
  case WM_CANCELMODE:
    release_mouse();
    return DefWindowProcW(window_, event, wparam, lparam);
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint();
    return 0;
  case WM_SIZE:
  case WM_MOVE:
    if (mouse_.captured())
      release_mouse();
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
    release_mouse();
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
  clear_host_input();
  gamepads_.stop();
  end_rewind();
  stop_fast_forward();
  paused_ = true;
  audio_.clear();
  if (options_.frames) {
    if (!options_.capture.empty()) {
      try {
        std::ofstream log(debugging_path(L".error.txt"));
        log << exception.what() << '\n';
        if (!log)
          throw std::runtime_error("Could not write the debugging report.");
      } catch (const std::exception &report_error) {
        OutputDebugStringA(report_error.what());
        MessageBoxA(window_, report_error.what(), "Cupid-N64 debugging output",
                    MB_OK | MB_ICONERROR);
      }
    }
    exit_code_ = 1;
    quit_ = true;
    return;
  }
  MessageBoxA(window_, exception.what(), "Cupid-N64", MB_OK | MB_ICONERROR);
}

void Window::rebase() {
  video_sync_available_ = true;
  measured_time_ = std::chrono::steady_clock::now();
  playback_clock_.reset(session_ ? session_->clocks() : 0, measured_time_);
  measured_frames_ = session_ ? session_->frames : 0;
  InvalidateRect(window_, nullptr, FALSE);
}

void Window::load(LaunchPreference preference) {
  if ((options_.rom.empty() && options_.ipl.empty()) || options_.firmware.empty())
    return;
  clear_host_input();
  end_rewind();
  stop_fast_forward();
  if (session_)
    session_->save();
  audio_.clear();
  gamepads_.stop();
  const auto controller_key = selected_controller_key();
  auto selected_settings = session_settings_;
  selected_settings.ports =
      options_.frames ? ControllerPorts{} : read_ports(settings_, controller_key);
  auto next = std::make_unique<Session>(
      options_.rom, options_.firmware, audio_, options_.ipl, options_.disk, options_.arcade_profile,
      options_.frames ? std::optional<std::uint64_t>(0) : std::nullopt, true, selected_settings);
  auto states = std::make_unique<StateSlots>(
      next->storage_image(), [this] { return session_->capture_state(); },
      [this](std::span<const std::uint8_t> bytes) { session_->restore_state(bytes); });
  states->select(state_slot_);
  auto rewind = std::make_unique<RewindHistory>(
      [this] { return session_->capture_state(); },
      [this](std::span<const std::uint8_t> bytes) { session_->restore_state(bytes); },
      rewind_settings_);
  rewind_ = std::move(rewind);
  states_ = std::move(states);
  session_ = std::move(next);
  if (preference == LaunchPreference::Remember)
    launch_on_restart_.reset();
  session_settings_ = std::move(selected_settings);
  controller_key_ = controller_key;
  feedback_.clear();
  reload_pending_ = false;
  state_message_.clear();
  game_image_ =
      options_.rom.empty() ? (options_.disk.empty() ? options_.ipl : options_.disk) : options_.rom;
  pixels_.clear();
  pixel_width_ = pixel_height_ = 0;
  frame_ready_ = false;
  last_frame_ = 0;
  paused_ = false;
  clear_host_input();
  rebase();
  update_states();
  update_playback();
  update_display();
  update_launch();
  update_controllers();
  update_graphics();
  update_shortcuts();
  if (!options_.frames) {
    if (options_.arcade_profile == n64::ArcadeProfile::Disabled)
      firmware_files_.n64 = options_.firmware;
    else
      firmware_files_.arcade = options_.firmware;
    if (!options_.ipl.empty())
      firmware_files_.ipl = options_.ipl;
    try {
      save_launch_settings();
    } catch (const std::exception &error) {
      status_error(L"Launch settings could not be saved", error);
    }
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
  const auto directory = firmware ? session_settings_.storage.directory(StorageLocation::Firmware,
                                                                        {}, application_directory())
                                  : game_image_.parent_path();
  if (firmware)
    validate_directory(directory, false);
  dialog.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&dialog) ? std::filesystem::path(path.data()) : std::filesystem::path{};
}

void Window::command(unsigned id) {
  if (settings_command(id))
    return;
  if (inspection_command(id))
    return;
  if (shader_command(id))
    return;
  if (graphics_command(id))
    return;
  if (input_tools_command(id))
    return;
  if (controller_command(id))
    return;
  if (launch_command(id))
    return;
  if (playback_settings_command(id))
    return;
  if (file_command(id))
    return;
  if (display_command(id))
    return;
  if (playback_command(id))
    return;
  if (state_command(id))
    return;
  if (id == Screenshot) {
    gamepads_.stop();
    screenshot();
  } else if (id == Exit) {
    SendMessageW(window_, WM_CLOSE, 0, 0);
  } else if (id == Pause) {
    end_rewind();
    stop_fast_forward();
    paused_ = !paused_;
    if (paused_) {
      gamepads_.stop();
      clear_host_input();
    }
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
    gamepads_.stop();
    feedback_.reset(feedback_state());
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
      clear_host_input();
      rebase();
    }
  } else if (id == FrameAdvance && session_ && paused_) {
    session_->advance_frame(inputs());
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
    gamepads_.stop();
    release_mouse();
    std::wstring text = help;
    text += L"\n\nCurrent shortcuts:\n";
    for (const auto action :
         {ShortcutAction::Pause, ShortcutAction::Reset, ShortcutAction::Mute,
          ShortcutAction::Fullscreen, ShortcutAction::Help, ShortcutAction::MouseCapture}) {
      const auto bindings = shortcut_text(shortcut_bindings_[static_cast<unsigned>(action)]);
      text += std::wstring(shortcut_name(action)) + L": " +
              (bindings.empty() ? L"Unassigned" : bindings) + L"\n";
    }
    text += L"\nView > Shortcuts edits these assignments.\n"
            L"Click the game image or use Capture mouse pointer when a mouse port is selected.\n"
            L"Escape releases a captured pointer.\n";
    text += L"\n\nView > Input mappings shows each player's device, buttons and axes.\n"
            L"Emulation > Controller ports selects the devices and accessories for this game.";
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
                L"File > Launch setup also starts 64DD and Aleck64 games.\n"
                L"Review local firmware and output folders in File.\n\n"
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
    if (session_ && session_->has_disk_drive())
      status = (session_->disk_image().empty()
                    ? L"64DD drive empty"
                    : L"64DD: " + session_->disk_image().filename().wstring()) +
               L"  |  " + status;
    if (reload_pending_)
      status = L"Settings pending: Emulation > Reload game  |  " + status;
    if (!state_message_.empty())
      status = state_message_ + L"  |  " + status;
    if (!shader_message_.empty())
      status = shader_message_ + L"  |  " + status;
    DrawTextW(dc, status.c_str(), -1, &bar, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    paint_buffer_.present(target);
  } catch (...) {
    EndPaint(window_, &painting);
    throw;
  }
  EndPaint(window_, &painting);
}

Window::Input Window::input() const {
  if (!active_ && focus_behavior_ != FocusBehavior::AllowInput)
    return {};
  const auto mapped = map_input(player_mappings_[0], host_inputs());
  const auto stick = n64::stick_response(mapped.x, mapped.y);
  auto buttons = mapped.buttons;
  auto x = stick.x;
  auto y = static_cast<std::int8_t>(-stick.y);
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
  session_->input(inputs());
  const auto interval_start = session_->clocks();
  do {
    session_->run();
  } while (session_->frames == last_frame_ && session_->clocks() - interval_start < 1875000);
  update_feedback();
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
  if (!fast_forward_ && playback_sync_ == PlaybackSync::Video)
    UpdateWindow(window_);
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
  std::wofstream report(debugging_path(L".txt"));
  report << L"Frames: " << session_->frames << L"\nAudio: "
         << (audio_.available() ? L"Ready" : audio_.error()) << L'\n';
  report.close();
  if (!report)
    throw std::runtime_error("Could not write the debugging report. Check folder permissions.");
}

} // namespace cupid::desktop
