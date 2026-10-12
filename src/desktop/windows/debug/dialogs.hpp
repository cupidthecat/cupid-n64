#pragma once

#include <windows.h>

namespace cupid::desktop {

class Session;
bool show_memory_inspection(HWND owner, Session &session);
void show_device_properties(HWND owner, const Session &session);

} // namespace cupid::desktop
