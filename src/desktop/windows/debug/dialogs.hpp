#pragma once

#include <windows.h>

namespace cupid::desktop {

class Session;
bool show_memory_inspection(HWND owner, Session &session);
void show_device_properties(HWND owner, const Session &session);
void show_disassembly(HWND owner, Session &session);
void show_instruction_trace(HWND owner, Session &session);
void show_core_diagnostics(HWND owner, Session &session);

} // namespace cupid::desktop
