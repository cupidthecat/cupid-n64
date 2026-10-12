#include "desktop/windows/commands.hpp"
#include "desktop/windows/debug/dialogs.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {

bool Window::inspection_command(unsigned id) {
  if (id != MemoryInspection && id != DeviceProperties && id != Disassembly &&
      id != InstructionTrace)
    return false;
  if (!session_)
    return true;
  end_rewind();
  stop_fast_forward();
  gamepads_.stop();
  clear_host_input();
  paused_ = true;
  audio_.clear();
  update_playback();
  try {
    session_->finish_frame();
    if (id == MemoryInspection) {
      if (show_memory_inspection(window_, *session_)) {
        if (rewind_)
          rewind_->clear();
        state_message_ = L"Memory edited; session paused";
      }
    } else if (id == DeviceProperties)
      show_device_properties(window_, *session_);
    else if (id == Disassembly)
      show_disassembly(window_, *session_);
    else
      show_instruction_trace(window_, *session_);
    refresh_frame();
  } catch (const std::exception &error) {
    status_error(L"Inspection", error);
  }
  rebase();
  update_states();
  return true;
}

} // namespace cupid::desktop
