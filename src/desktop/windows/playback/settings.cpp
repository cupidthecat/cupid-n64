#include "desktop/windows/audio/identity.hpp"
#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {

void Window::load_playback_settings() {
  if (settings_.empty())
    return;
  const auto playback = [&](const wchar_t *key, unsigned maximum) {
    const auto value = GetPrivateProfileIntW(L"Playback", key, 0, settings_.c_str());
    return value <= maximum ? value : 0;
  };
  playback_sync_ = static_cast<PlaybackSync>(playback(L"Sync", 1));
  focus_behavior_ = static_cast<FocusBehavior>(playback(L"Focus", 2));
  auto audio = audio_.settings();
  const auto volume = GetPrivateProfileIntW(L"Audio", L"Volume", 100, settings_.c_str());
  audio.levels.volume = volume <= 200 ? volume : 100;
  const auto balance =
      static_cast<int>(GetPrivateProfileIntW(L"Audio", L"Balance", 0, settings_.c_str()));
  audio.levels.balance = balance >= -100 && balance <= 100 ? balance : 0;
  const auto latency = GetPrivateProfileIntW(L"Audio", L"Latency", 40, settings_.c_str());
  audio.latency =
      std::find(audio_latencies.begin(), audio_latencies.end(), latency) != audio_latencies.end()
          ? latency
          : 40;
  std::array<wchar_t, 32768> device{};
  GetPrivateProfileStringW(L"Audio", L"Device", L"", device.data(),
                           static_cast<DWORD>(device.size()), settings_.c_str());
  audio.device = audio_endpoint_key(device.data());
  audio_.configure(std::move(audio));
}

void Window::save_playback_settings() {
  if (settings_.empty() || options_.frames)
    return;
  const auto write = [&](const wchar_t *section, const wchar_t *key, auto value) {
    return WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(),
                                      settings_.c_str()) != 0;
  };
  const auto &audio = audio_.settings();
  if (!write(L"Playback", L"Sync", static_cast<unsigned>(playback_sync_)) ||
      !write(L"Playback", L"Focus", static_cast<unsigned>(focus_behavior_)) ||
      !write(L"Audio", L"Volume", audio.levels.volume) ||
      !write(L"Audio", L"Balance", audio.levels.balance) ||
      !write(L"Audio", L"Latency", audio.latency) ||
      !WritePrivateProfileStringW(L"Audio", L"Device", audio.device.c_str(), settings_.c_str()))
    throw std::runtime_error("Could not save playback settings. Check folder permissions.");
}

void Window::create_playback_settings_menu(HMENU emulation) {
  audio_menu_ = CreatePopupMenu();
  AppendMenuW(audio_menu_, MF_STRING | MF_GRAYED, 0, L"Active output");
  const auto volume = CreatePopupMenu(), balance = CreatePopupMenu(), latency = CreatePopupMenu();
  for (unsigned n = 0; n <= 20; ++n) {
    const auto label = std::to_wstring(n * 10) + L"%";
    AppendMenuW(volume, MF_STRING, AudioVolumeFirst + n, label.c_str());
  }
  for (unsigned n = 0; n <= 8; ++n) {
    const int value = static_cast<int>(n) * 25 - 100;
    const auto label =
        value == 0 ? L"Center"
                   : std::to_wstring(std::abs(value)) + (value < 0 ? L"% left" : L"% right");
    AppendMenuW(balance, MF_STRING, AudioBalanceFirst + n, label.c_str());
  }
  for (unsigned n = 0; n < audio_latencies.size(); ++n) {
    const auto label = std::to_wstring(audio_latencies[n]) + L" ms";
    AppendMenuW(latency, MF_STRING, AudioLatencyFirst + n, label.c_str());
  }
  audio_device_menu_ = CreatePopupMenu();
  AppendMenuW(audio_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(volume), L"&Volume");
  AppendMenuW(audio_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(balance), L"Stereo &balance");
  AppendMenuW(audio_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(latency), L"Output &latency");
  AppendMenuW(audio_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(audio_device_menu_),
              L"Output &device");
  AppendMenuW(emulation, MF_POPUP, reinterpret_cast<UINT_PTR>(audio_menu_), L"&Audio");
  const auto sync = CreatePopupMenu(), focus = CreatePopupMenu();
  AppendMenuW(sync, MF_STRING, SyncAudio, L"&Audio output");
  AppendMenuW(sync, MF_STRING, SyncVideo, L"&Monitor refresh");
  AppendMenuW(focus, MF_STRING, FocusPause, L"&Pause");
  AppendMenuW(focus, MF_STRING, FocusBlockInput, L"Continue, &block game input");
  AppendMenuW(focus, MF_STRING, FocusAllowInput, L"Continue, &allow game input");
  AppendMenuW(emulation, MF_POPUP, reinterpret_cast<UINT_PTR>(sync), L"Playback &synchronization");
  AppendMenuW(emulation, MF_POPUP, reinterpret_cast<UINT_PTR>(focus), L"When &unfocused");
  refresh_audio_devices();
}

void Window::refresh_audio_devices() {
  audio_.recover();
  audio_devices_.clear();
  for (const auto &device : audio_.devices())
    audio_devices_.push_back(
        {static_cast<unsigned>(audio_devices_.size()), device.identity, device.name});
  if (audio_device_menu_) {
    while (GetMenuItemCount(audio_device_menu_) > 0)
      DeleteMenu(audio_device_menu_, 0, MF_BYPOSITION);
    AppendMenuW(audio_device_menu_, MF_STRING, AudioDeviceDefault, L"&System default");
    for (unsigned n = 0; n < audio_devices_.size(); ++n)
      AppendMenuW(audio_device_menu_, MF_STRING, AudioDeviceFirst + n,
                  audio_devices_[n].name.c_str());
    const auto &selected = audio_.settings().device;
    if (!selected.empty() &&
        std::none_of(audio_devices_.begin(), audio_devices_.end(), [&](const auto &device) {
          return same_audio_endpoint(device.identity, selected);
        }))
      AppendMenuW(audio_device_menu_, MF_STRING | MF_GRAYED | MF_CHECKED, 0,
                  L"Selected output is unavailable; using fallback");
  }
  update_playback_settings();
}

void Window::update_playback_settings() {
  const auto &audio = audio_.settings();
  CheckMenuRadioItem(menu_, SyncAudio, SyncVideo,
                     playback_sync_ == PlaybackSync::Audio ? SyncAudio : SyncVideo, MF_BYCOMMAND);
  CheckMenuRadioItem(menu_, FocusPause, FocusAllowInput,
                     FocusPause + static_cast<unsigned>(focus_behavior_), MF_BYCOMMAND);
  for (unsigned n = 0; n <= 20; ++n)
    CheckMenuItem(audio_menu_, AudioVolumeFirst + n,
                  MF_BYCOMMAND | (audio.levels.volume == n * 10 ? MF_CHECKED : MF_UNCHECKED));
  for (unsigned n = 0; n <= 8; ++n)
    CheckMenuItem(
        audio_menu_, AudioBalanceFirst + n,
        MF_BYCOMMAND |
            (audio.levels.balance == static_cast<int>(n) * 25 - 100 ? MF_CHECKED : MF_UNCHECKED));
  for (unsigned n = 0; n < audio_latencies.size(); ++n)
    CheckMenuItem(audio_menu_, AudioLatencyFirst + n,
                  MF_BYCOMMAND | (audio.latency == audio_latencies[n] ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(audio_device_menu_, AudioDeviceDefault,
                MF_BYCOMMAND | (audio.device.empty() ? MF_CHECKED : MF_UNCHECKED));
  for (unsigned n = 0; n < audio_devices_.size(); ++n)
    CheckMenuItem(audio_device_menu_, AudioDeviceFirst + n,
                  MF_BYCOMMAND | (same_audio_endpoint(audio.device, audio_devices_[n].identity)
                                      ? MF_CHECKED
                                      : MF_UNCHECKED));
  if (audio_menu_) {
    const auto label = audio_.available() ? L"Active: " + audio_.device_name() +
                                                (audio_.using_fallback() ? L" (fallback)" : L"")
                                          : L"Audio unavailable: " + audio_.error();
    ModifyMenuW(audio_menu_, 0, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, label.c_str());
  }
  if (window_)
    DrawMenuBar(window_);
}

bool Window::playback_settings_command(unsigned id) {
  const bool volume = id >= AudioVolumeFirst && id <= AudioVolumeFirst + 20;
  const bool balance = id >= AudioBalanceFirst && id <= AudioBalanceFirst + 8;
  const bool latency = id >= AudioLatencyFirst && id < AudioLatencyFirst + audio_latencies.size();
  const bool device = id >= AudioDeviceFirst && id < AudioDeviceFirst + audio_devices_.size();
  const bool focus = id >= FocusPause && id <= FocusAllowInput;
  if (id != SyncAudio && id != SyncVideo && !focus && !volume && !balance && !latency && !device &&
      id != AudioDeviceDefault)
    return false;
  if (id == SyncAudio || id == SyncVideo) {
    playback_sync_ = id == SyncAudio ? PlaybackSync::Audio : PlaybackSync::Video;
    rebase();
  } else if (focus) {
    const bool was_stopped = stopped();
    end_rewind();
    stop_fast_forward();
    clear_host_input();
    focus_behavior_ = static_cast<FocusBehavior>(id - FocusPause);
    if (was_stopped != stopped()) {
      audio_.clear();
      rebase();
    }
  } else {
    auto audio = audio_.settings();
    if (volume)
      audio.levels.volume = (id - AudioVolumeFirst) * 10;
    if (balance)
      audio.levels.balance = static_cast<int>(id - AudioBalanceFirst) * 25 - 100;
    if (latency)
      audio.latency = audio_latencies[id - AudioLatencyFirst];
    if (device || id == AudioDeviceDefault)
      audio.device = device ? audio_devices_[id - AudioDeviceFirst].identity : L"";
    audio_.configure(std::move(audio));
    if (!audio_.available())
      state_message_ = L"Audio unavailable: " + audio_.error();
  }
  try {
    save_playback_settings();
  } catch (const std::exception &error) {
    status_error(L"Playback settings could not be saved", error);
  }
  update_playback_settings();
  InvalidateRect(window_, nullptr, FALSE);
  return true;
}

} // namespace cupid::desktop
