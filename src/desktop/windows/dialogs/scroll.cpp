#include "desktop/windows/dialogs/scroll.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include <stdexcept>

namespace cupid::desktop {
namespace {

LRESULT CALLBACK content_procedure(HWND window, UINT event, WPARAM wparam, LPARAM lparam) {
  if (event == WM_COMMAND || event == WM_NOTIFY || event == WM_MOUSEWHEEL ||
      event == WM_MOUSEHWHEEL)
    return SendMessageW(GetParent(window), event, wparam, lparam);
  return DefWindowProcW(window, event, wparam, lparam);
}

} // namespace

void ScrollForm::initialize(HWND dialog, int footer_y) {
  dialog_ = dialog;
  RECT client{}, footer{0, 0, 0, footer_y}, line{0, 0, 0, 12};
  GetClientRect(dialog, &client);
  MapDialogRect(dialog, &footer);
  MapDialogRect(dialog, &line);
  width_ = client.right;
  height_ = client.bottom;
  footer_ = footer.bottom;
  footer_height_ = height_ - footer_;
  line_ = line.bottom;
  for (auto child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
    RECT bounds{};
    GetWindowRect(child, &bounds);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT *>(&bounds), 2);
    (bounds.top >= footer_ ? buttons_ : fields_).push_back({child, bounds});
  }
  WNDCLASSW type{};
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpszClassName = L"CupidScrollableForm";
  if (!GetClassInfoW(type.hInstance, type.lpszClassName, &type)) {
    type.lpfnWndProc = content_procedure;
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
    if (!RegisterClassW(&type))
      throw std::runtime_error("Could not create the settings scroll area.");
  }
  content_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"CupidScrollableForm", L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, width_, footer_, dialog,
                             reinterpret_cast<HMENU>(UINT_PTR(content_id)),
                             GetModuleHandleW(nullptr), nullptr);
  if (!content_)
    throw std::runtime_error("Could not create the settings scroll area.");
  SendMessageW(content_, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), FALSE);
  for (const auto &field : fields_)
    SetParent(field.window, content_);
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor))
    throw std::runtime_error("Could not determine the settings window work area.");
  fit(monitor.rcWork);
}

void ScrollForm::fit(const RECT &work_area) {
  RECT outer{}, current{};
  GetWindowRect(dialog_, &outer);
  GetClientRect(dialog_, &current);
  const int decoration_x = outer.right - outer.left - current.right;
  const int decoration_y = outer.bottom - outer.top - current.bottom;
  const auto width = std::min(width_ + decoration_x, int(work_area.right - work_area.left) - 16);
  const auto height = std::min(height_ + decoration_y, int(work_area.bottom - work_area.top) - 16);
  auto style = GetWindowLongPtrW(dialog_, GWL_STYLE);
  style &= ~(WS_HSCROLL | WS_VSCROLL);
  if (width < width_ + decoration_x || height < height_ + decoration_y)
    style |= WS_HSCROLL | WS_VSCROLL;
  SetWindowLongPtrW(dialog_, GWL_STYLE, style);
  SetWindowPos(dialog_, nullptr, work_area.left + 8, work_area.top + 8, width, height,
               SWP_NOZORDER | SWP_FRAMECHANGED);
  RECT client{};
  GetClientRect(dialog_, &client);
  footer_height_ = height_ - footer_;
  int left_edge = 0, right_width = 0, button_height = 0;
  for (const auto &button : buttons_) {
    if (GetDlgCtrlID(button.window) == IDOK || GetDlgCtrlID(button.window) == IDCANCEL)
      right_width += button.bounds.right - button.bounds.left + line_;
    else
      left_edge = std::max(left_edge, int(button.bounds.right));
    button_height = std::max(button_height, int(button.bounds.bottom - button.bounds.top));
  }
  const bool wrap = left_edge && left_edge + right_width + line_ > client.right;
  if (wrap)
    footer_height_ += button_height + line_ / 2;
  const int footer_top = client.bottom - footer_height_;
  for (const auto &button : buttons_) {
    const auto id = GetDlgCtrlID(button.window);
    const bool right = id == IDOK || id == IDCANCEL;
    const auto x = button.bounds.left + (right ? client.right - width_ : 0);
    auto y = footer_top + button.bounds.top - footer_;
    if (right && wrap)
      y += button_height + line_ / 2;
    SetWindowPos(button.window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  }
  SetWindowPos(content_, nullptr, 0, 0, client.right, footer_top, SWP_NOZORDER);
  SCROLLINFO horizontal{sizeof(SCROLLINFO),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        width_ - 1,
                        static_cast<UINT>(client.right),
                        0,
                        0};
  SCROLLINFO vertical{sizeof(SCROLLINFO),
                      SIF_RANGE | SIF_PAGE | SIF_POS,
                      0,
                      footer_ - 1,
                      static_cast<UINT>(footer_top),
                      0,
                      0};
  SetScrollInfo(dialog_, SB_HORZ, &horizontal, TRUE);
  SetScrollInfo(dialog_, SB_VERT, &vertical, TRUE);
  x_ = y_ = 0;
  position();
}

void ScrollForm::position() {
  for (const auto &field : fields_)
    SetWindowPos(field.window, nullptr, field.bounds.left - x_, field.bounds.top - y_, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
  InvalidateRect(content_, nullptr, TRUE);
}

bool ScrollForm::message(UINT event, WPARAM wparam) {
  if (!content_ || (event != WM_VSCROLL && event != WM_HSCROLL && event != WM_MOUSEWHEEL &&
                    event != WM_MOUSEHWHEEL))
    return false;
  const bool horizontal = event == WM_HSCROLL || event == WM_MOUSEHWHEEL;
  const auto bar = horizontal ? SB_HORZ : SB_VERT;
  SCROLLINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = SIF_ALL;
  GetScrollInfo(dialog_, bar, &info);
  auto position = info.nPos;
  if (event == WM_MOUSEWHEEL || event == WM_MOUSEHWHEEL) {
    position += GET_WHEEL_DELTA_WPARAM(wparam) * line_ * (horizontal ? 3 : -3) / WHEEL_DELTA;
  } else {
    switch (LOWORD(wparam)) {
    case SB_LINEUP:
      position -= line_;
      break;
    case SB_LINEDOWN:
      position += line_;
      break;
    case SB_PAGEUP:
      position -= static_cast<int>(info.nPage);
      break;
    case SB_PAGEDOWN:
      position += static_cast<int>(info.nPage);
      break;
    case SB_THUMBTRACK:
      position = info.nTrackPos;
      break;
    case SB_TOP:
      position = 0;
      break;
    case SB_BOTTOM:
      position = info.nMax;
      break;
    }
  }
  info.fMask = SIF_POS;
  info.nPos = std::clamp(position, 0, std::max(0, info.nMax - int(info.nPage) + 1));
  SetScrollInfo(dialog_, bar, &info, TRUE);
  (horizontal ? x_ : y_) = info.nPos;
  this->position();
  return true;
}

} // namespace cupid::desktop
