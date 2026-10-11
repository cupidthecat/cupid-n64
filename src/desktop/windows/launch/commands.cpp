#include "desktop/windows/commands.hpp"
#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {

void Window::create_launch_menus(HMENU file, HMENU emulation) {
  AppendMenuW(file, MF_STRING, Launch, L"&Launch setup...");
  AppendMenuW(file, MF_STRING, Firmware, L"Local &firmware settings...");
  AppendMenuW(file, MF_STRING, Folders, L"&Output and firmware folders...");
  AppendMenuW(file, MF_STRING | MF_GRAYED, DiskChange, L"Insert / &change 64DD disk...");
  AppendMenuW(file, MF_STRING | MF_GRAYED, DiskEject, L"&Eject 64DD disk (drive empty)");
  memory_menu_ = CreatePopupMenu();
  AppendMenuW(memory_menu_, MF_STRING, ExpansionPak, L"&Expansion Pak (8 MiB RDRAM)");
  AppendMenuW(memory_menu_, MF_STRING | MF_GRAYED, 0, L"Changes take effect when the game reloads");
  AppendMenuW(memory_menu_, MF_STRING | MF_GRAYED, 0,
              L"Larger Paks use separate files; old saves are kept");
  for (unsigned port = 0; port < 4; ++port) {
    const auto pak = CreatePopupMenu();
    for (unsigned n = 0; n < MemorySettings::capacities.size(); ++n) {
      const auto label = std::to_wstring(MemorySettings::capacities[n] * 32) + L" KiB";
      AppendMenuW(pak, MF_STRING, PakCapacityFirst + port * 4 + n, label.c_str());
    }
    const auto label = L"Port " + std::to_wstring(port + 1) + L" Controller Pak";
    AppendMenuW(memory_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(pak), label.c_str());
  }
  AppendMenuW(emulation, MF_POPUP, reinterpret_cast<UINT_PTR>(memory_menu_), L"&Memory settings");
  AppendMenuW(emulation, MF_STRING | MF_GRAYED, Reload, L"Reload game with selected settings");
}

void Window::update_launch() {
  if (!menu_)
    return;
  const bool drive = session_ && session_->has_disk_drive();
  const bool inserted = drive && !session_->disk_image().empty();
  EnableMenuItem(menu_, DiskChange, MF_BYCOMMAND | (drive ? MF_ENABLED : MF_GRAYED));
  const auto eject = inserted ? L"Eject 64DD disk: " + session_->disk_image().filename().wstring()
                              : L"Eject 64DD disk (drive empty)";
  ModifyMenuW(menu_, DiskEject, MF_BYCOMMAND | MF_STRING | (inserted ? MF_ENABLED : MF_GRAYED),
              DiskEject, eject.c_str());
  CheckMenuItem(menu_, ExpansionPak,
                MF_BYCOMMAND | (session_settings_.memory.expansion ? MF_CHECKED : MF_UNCHECKED));
  for (unsigned port = 0; port < 4; ++port)
    for (unsigned n = 0; n < MemorySettings::capacities.size(); ++n)
      CheckMenuItem(menu_, PakCapacityFirst + port * 4 + n,
                    MF_BYCOMMAND |
                        (session_settings_.memory.pak_banks[port] == MemorySettings::capacities[n]
                             ? MF_CHECKED
                             : MF_UNCHECKED));
  ModifyMenuW(menu_, Reload, MF_BYCOMMAND | MF_STRING | (session_ ? MF_ENABLED : MF_GRAYED), Reload,
              reload_pending_ ? L"Reload game to apply pending settings"
                              : L"Reload game with selected settings");
  DrawMenuBar(window_);
  InvalidateRect(window_, nullptr, FALSE);
}

void Window::disk_changed() {
  options_.disk = session_->disk_image();
  const bool controllers_changed = stage_controller_settings();
  if (options_.rom.empty())
    game_image_ = options_.disk.empty() ? options_.ipl : options_.disk;
  states_ = std::make_unique<StateSlots>(
      session_->storage_image(), [this] { return session_->capture_state(); },
      [this](std::span<const std::uint8_t> bytes) { session_->restore_state(bytes); });
  states_->select(state_slot_);
  rewind_->clear();
  audio_.clear();
  state_message_ = options_.disk.empty()
                       ? L"64DD drive empty"
                       : L"64DD disk inserted: " + options_.disk.filename().wstring();
  if (controllers_changed)
    state_message_ += L"; reload the game to apply its controller setup";
  update_states();
  save_launch_settings();
}

bool Window::launch_command(unsigned id) {
  const bool capacity = id >= PakCapacityFirst && id < PakCapacityFirst + 16;
  if (!capacity && id != Launch && id != Firmware && id != Folders && id != DiskChange &&
      id != DiskEject && id != ExpansionPak && id != Reload)
    return false;
  end_rewind();
  stop_fast_forward();
  clear_host_input();
  try {
    if (id == Launch) {
      launch_dialog();
    } else if (id == Firmware) {
      firmware_dialog();
    } else if (id == Folders) {
      folders_dialog();
    } else if (id == ExpansionPak || capacity) {
      if (capacity) {
        const auto choice = id - PakCapacityFirst;
        session_settings_.memory.pak_banks[choice / 4] = MemorySettings::capacities[choice % 4];
      } else {
        session_settings_.memory.expansion = !session_settings_.memory.expansion;
      }
      reload_pending_ |= session_ != nullptr;
      state_message_ = session_ ? L"Memory settings saved; reload the game to apply them"
                                : L"Memory settings saved for the next game";
      save_launch_settings();
    } else if (id == Reload && session_) {
      validate_launch(options_);
      load();
    } else if (id == DiskChange && session_ && session_->has_disk_drive()) {
      const auto current = session_->disk_image();
      const auto directory = current.empty() ? options_.ipl.parent_path() : current.parent_path();
      if (const auto selected = select_file(window_, L"Insert or change a 64DD disk",
                                            FileSelection::Disk, directory, current)) {
        session_->change_disk(*selected);
        disk_changed();
      }
    } else if (id == DiskEject && session_ && !session_->disk_image().empty()) {
      session_->eject_disk();
      disk_changed();
    }
  } catch (const std::exception &error) {
    status_error(L"Launch or settings action failed", error);
  }
  rebase();
  update_launch();
  return true;
}

} // namespace cupid::desktop
