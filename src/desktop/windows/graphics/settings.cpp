#include "desktop/windows/commands.hpp"
#include "desktop/windows/window.hpp"
#include <stdexcept>

namespace cupid::desktop {

void Window::load_graphics_settings() {
  if (settings_.empty())
    return;
  n64::RenderSettings selected;
  selected.scale = GetPrivateProfileIntW(L"Graphics", L"InternalScale", 1, settings_.c_str());
  selected.supersampling =
      GetPrivateProfileIntW(L"Graphics", L"Supersampling", 0, settings_.c_str()) == 1;
  selected.weave = GetPrivateProfileIntW(L"Graphics", L"Weave", 0, settings_.c_str()) == 1;
  selected.bypass_vi = GetPrivateProfileIntW(L"Graphics", L"BypassVI", 0, settings_.c_str()) == 1;
  session_settings_.graphics = selected.valid() ? selected : n64::RenderSettings{};
}

void Window::save_graphics_settings(n64::RenderSettings selected) {
  if (settings_.empty() || options_.frames)
    return;
  const std::wstring section = L"InternalScale=" + std::to_wstring(selected.scale) + L'\0' +
                               L"Supersampling=" + (selected.supersampling ? L"1" : L"0") + L'\0' +
                               L"Weave=" + (selected.weave ? L"1" : L"0") + L'\0' + L"BypassVI=" +
                               (selected.bypass_vi ? L"1" : L"0") + L'\0';
  if (!WritePrivateProfileSectionW(L"Graphics", section.c_str(), settings_.c_str()))
    throw std::runtime_error("Could not save graphics settings. Check folder permissions.");
}

void Window::create_graphics_menu(HMENU view) {
  graphics_menu_ = CreatePopupMenu();
  const auto resolution = CreatePopupMenu();
  for (unsigned index = 0; index < n64::RenderSettings::scales.size(); ++index) {
    const auto scale = n64::RenderSettings::scales[index];
    const auto label = scale == 1 ? std::wstring(L"&Native (1x)")
                                  : L"&" + std::to_wstring(scale) + L"x internal resolution";
    AppendMenuW(resolution, MF_STRING, RenderScaleFirst + index, label.c_str());
  }
  AppendMenuW(graphics_menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(resolution),
              L"Internal &resolution (reload required)");
  AppendMenuW(graphics_menu_, MF_STRING, RenderSupersampling,
              L"&Supersampling to native size (2x/4x/8x; excludes weave)");
  AppendMenuW(graphics_menu_, MF_STRING, RenderWeave,
              L"&Weave deinterlacing (excludes supersampling)");
  AppendMenuW(graphics_menu_, MF_STRING, RenderBypassVI, L"&Bypass VI image processing");
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(graphics_menu_), L"&Graphics");
  update_graphics();
}

void Window::update_graphics() {
  if (!graphics_menu_)
    return;
  const auto selected = session_settings_.graphics;
  const bool available =
      VideoOutput::supports_hardware() && (!session_ || session_->hardware_rendering());
  const auto update = [&](unsigned id, bool checked, bool enabled) {
    CheckMenuItem(graphics_menu_, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(graphics_menu_, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
  };
  for (unsigned index = 0; index < n64::RenderSettings::scales.size(); ++index) {
    const auto scale = n64::RenderSettings::scales[index];
    update(RenderScaleFirst + index, selected.scale == scale,
           available && (scale > 1 || !selected.supersampling));
  }
  update(RenderSupersampling, selected.supersampling,
         available && selected.scale > 1 && !selected.weave);
  update(RenderWeave, selected.weave, available && !selected.supersampling);
  update(RenderBypassVI, selected.bypass_vi, available);
  if (window_)
    DrawMenuBar(window_);
}

bool Window::graphics_command(unsigned id) {
  const bool scale =
      id >= RenderScaleFirst && id < RenderScaleFirst + n64::RenderSettings::scales.size();
  if (!scale && id != RenderSupersampling && id != RenderWeave && id != RenderBypassVI)
    return false;
  if (!VideoOutput::supports_hardware() || (session_ && !session_->hardware_rendering()))
    return true;
  auto selected = session_settings_.graphics;
  if (scale)
    selected.scale = n64::RenderSettings::scales[id - RenderScaleFirst];
  else if (id == RenderSupersampling)
    selected.supersampling = !selected.supersampling;
  else if (id == RenderWeave)
    selected.weave = !selected.weave;
  else
    selected.bypass_vi = !selected.bypass_vi;
  if (!selected.valid())
    return true;
  end_rewind();
  stop_fast_forward();
  clear_host_input();
  try {
    save_graphics_settings(selected);
    const bool reload = session_ && selected.scale != session_->graphics_settings().scale;
    if (session_ && !reload) {
      session_->graphics_settings(selected);
      if (rewind_)
        rewind_->clear();
    }
    session_settings_.graphics = selected;
    reload_pending_ |= reload;
    state_message_ = reload ? L"Graphics settings saved; reload to change internal resolution"
                            : L"Graphics settings saved";
  } catch (const std::exception &error) {
    status_error(L"Graphics setting failed", error);
  }
  update_graphics();
  rebase();
  return true;
}

} // namespace cupid::desktop
