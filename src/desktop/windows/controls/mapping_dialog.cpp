#include "desktop/windows/controls/mapping_dialog.hpp"
#include "desktop/windows/controls/keys.hpp"
#include "desktop/windows/dialogs/form.hpp"
#include "desktop/windows/dialogs/scroll.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {
namespace {

constexpr unsigned player_id = 90, source_id = 91, status_id = 92, walk_id = 93, feedback_id = 94,
                   defaults_id = 95, clear_id = 96;
constexpr unsigned buttons_first = 100, axes_first = 200, negative_first = 300,
                   positive_first = 400, inverted_first = 500;

struct Choice {
  std::wstring name;
  InputBinding binding;
};

struct DialogState {
  MappingForm form;
  unsigned player = 0;
  std::vector<Choice> choices;
  ScrollForm layout;
};

std::vector<Choice> bindings(HostSource source) {
  std::vector<Choice> choices{{L"None", {}}};
  if (source < HostSource::Gamepad1) {
    for (unsigned key = 8; key < 256; ++key)
      choices.push_back({key_name(key), {BindingKind::Key, key}});
  } else {
    constexpr std::array names{L"A",
                               L"B",
                               L"X",
                               L"Y",
                               L"Left shoulder",
                               L"Right shoulder",
                               L"Back",
                               L"Start",
                               L"Left stick press",
                               L"Right stick press",
                               L"D-pad Up",
                               L"D-pad Down",
                               L"D-pad Left",
                               L"D-pad Right"};
    constexpr std::array<unsigned, 14> masks{0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200, 0x20,
                                             0x10,   0x40,   0x80,   1,      2,     4,     8};
    for (unsigned n = 0; n < names.size(); ++n)
      choices.push_back({names[n], {BindingKind::GamepadButton, masks[n]}});
    constexpr std::array negative{L"Left stick left", L"Left stick up", L"Right stick left",
                                  L"Right stick up"};
    constexpr std::array positive{L"Left stick right", L"Left stick down", L"Right stick right",
                                  L"Right stick down", L"Left trigger",    L"Right trigger"};
    for (unsigned n = 0; n < negative.size(); ++n)
      choices.push_back({negative[n], {BindingKind::AxisNegative, n}});
    for (unsigned n = 0; n < positive.size(); ++n)
      choices.push_back({positive[n], {BindingKind::AxisPositive, n}});
  }
  return choices;
}

void fill(HWND dialog, unsigned id, InputBinding selected, const std::vector<Choice> &choices) {
  const auto control = GetDlgItem(dialog, id);
  SendMessageW(control, WM_SETREDRAW, FALSE, 0);
  SendMessageW(control, CB_RESETCONTENT, 0, 0);
  unsigned index = 0;
  for (unsigned n = 0; n < choices.size(); ++n) {
    SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choices[n].name.c_str()));
    if (choices[n].binding == selected)
      index = n;
  }
  SendMessageW(control, CB_SETCURSEL, index, 0);
  SendMessageW(control, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(control, nullptr, TRUE);
}

void status(HWND dialog, const DialogState &state) {
  const auto &mapping = state.form.mappings[state.player];
  std::wstring text;
  if (state.form.controllers[state.player].controller == ControllerType::Mouse)
    text = L"Mouse: capture the pointer in the game image or in View. Escape releases it.\n"
           L"These keyboard/gamepad bindings apply to other controller types.";
  else if (mapping.source == HostSource::None)
    text = L"This player's host input is disabled.";
  else if (mapping.source == HostSource::Keyboard)
    text = L"Keyboard buttons and directions. Walk scales the main stick.";
  else if (!state.form.available)
    text = L"XInput gamepad input is unavailable on this system.";
  else if (state.form.poll) {
    const auto pads = state.form.poll();
    const auto &pad =
        pads[static_cast<unsigned>(mapping.source) - static_cast<unsigned>(HostSource::Gamepad1)];
    text = !pad.connected ? L"Gamepad disconnected; its buttons and axes are released."
           : pad.feedback ? L"Gamepad connected; rumble is available."
                          : L"Gamepad connected; rumble is unavailable on this device.";
  }
  if (state.form.controllers[state.player].controller == ControllerType::Disconnected)
    text += L"\nThis N64 port is disconnected. Connect it in Emulation > Controller ports.";
  SetDlgItemTextW(dialog, status_id, text.c_str());
}

void refresh(HWND dialog, DialogState &state) {
  const auto &mapping = state.form.mappings[state.player];
  state.choices = bindings(mapping.source);
  const auto retain = [&](InputBinding binding) {
    if (std::any_of(state.choices.begin(), state.choices.end(),
                    [&](const Choice &choice) { return choice.binding == binding; }))
      return;
    auto name = binding.kind == BindingKind::Key
                    ? L"Keyboard: " + key_name(binding.code)
                    : L"Binding " + std::to_wstring(static_cast<unsigned>(binding.kind)) + L":" +
                          std::to_wstring(binding.code);
    state.choices.push_back({std::move(name), binding});
  };
  for (const auto binding : mapping.buttons)
    retain(binding);
  for (const auto &axis : mapping.axes) {
    retain(axis.negative);
    retain(axis.positive);
  }
  retain(mapping.slow);
  SendDlgItemMessageW(dialog, source_id, CB_SETCURSEL, static_cast<unsigned>(mapping.source), 0);
  for (unsigned n = 0; n < mapping.buttons.size(); ++n)
    fill(dialog, buttons_first + n, mapping.buttons[n], state.choices);
  for (unsigned n = 0; n < mapping.axes.size(); ++n) {
    const auto &axis = mapping.axes[n];
    SendDlgItemMessageW(dialog, axes_first + n, CB_SETCURSEL, static_cast<unsigned>(axis.source),
                        0);
    EnableWindow(GetDlgItem(dialog, axes_first + n), mapping.source >= HostSource::Gamepad1);
    fill(dialog, negative_first + n, axis.negative, state.choices);
    fill(dialog, positive_first + n, axis.positive, state.choices);
    SendDlgItemMessageW(dialog, inverted_first + n, BM_SETCHECK,
                        axis.inverted ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  fill(dialog, walk_id, mapping.slow, state.choices);
  SendDlgItemMessageW(dialog, feedback_id, BM_SETCHECK,
                      mapping.feedback ? BST_CHECKED : BST_UNCHECKED, 0);
  status(dialog, state);
}

void read(HWND dialog, DialogState &state) {
  auto &mapping = state.form.mappings[state.player];
  const auto binding = [&](unsigned id) {
    const auto selected = SendDlgItemMessageW(dialog, id, CB_GETCURSEL, 0, 0);
    if (selected < 0 || static_cast<std::size_t>(selected) >= state.choices.size())
      throw std::runtime_error("Choose a binding for each field, or select None.");
    return state.choices[static_cast<std::size_t>(selected)].binding;
  };
  for (unsigned n = 0; n < mapping.buttons.size(); ++n)
    mapping.buttons[n] = binding(buttons_first + n);
  for (unsigned n = 0; n < mapping.axes.size(); ++n) {
    auto &axis = mapping.axes[n];
    axis.source =
        static_cast<HostAxis>(SendDlgItemMessageW(dialog, axes_first + n, CB_GETCURSEL, 0, 0));
    axis.inverted =
        SendDlgItemMessageW(dialog, inverted_first + n, BM_GETCHECK, 0, 0) == BST_CHECKED;
    axis.negative = binding(negative_first + n);
    axis.positive = binding(positive_first + n);
  }
  mapping.slow = binding(walk_id);
  mapping.feedback = SendDlgItemMessageW(dialog, feedback_id, BM_GETCHECK, 0, 0) == BST_CHECKED;
  validate_mapping(mapping);
}

INT_PTR CALLBACK procedure(HWND dialog, UINT event, WPARAM wparam, LPARAM lparam) {
  auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (event == WM_INITDIALOG) {
    state = reinterpret_cast<DialogState *>(lparam);
    SetWindowLongPtrW(dialog, DWLP_USER, lparam);
    dialog_control(dialog, L"STATIC", L"Player:", 0, 0, 10, 13, 45, 12);
    const auto player = dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                       player_id, 60, 10, 100, 120);
    for (unsigned port = 1; port <= 4; ++port) {
      const auto name = L"Port " + std::to_wstring(port);
      SendMessageW(player, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(player, CB_SETCURSEL, 0, 0);
    dialog_control(dialog, L"STATIC", L"Device:", 0, 0, 175, 13, 45, 12);
    const auto source = dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                       source_id, 225, 10, 240, 160);
    for (const auto name :
         {L"None", L"Keyboard", L"XInput gamepad slot 1", L"XInput gamepad slot 2",
          L"XInput gamepad slot 3", L"XInput gamepad slot 4"})
      SendMessageW(source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    dialog_control(dialog, L"STATIC", L"", 0, status_id, 10, 36, 660, 38);
    constexpr DWORD dropdown = CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL;
    for (unsigned n = 0; n < button_names.size(); ++n) {
      const int x = n < 8 ? 10 : 350, y = 88 + int(n % 8) * 19;
      dialog_control(dialog, L"STATIC", button_names[n].data(), 0, 0, x, y + 3, 105, 12);
      dialog_control(dialog, L"COMBOBOX", L"", dropdown, buttons_first + n, x + 110, y, 210, 180);
    }
    dialog_control(dialog, L"STATIC", L"Analog source", 0, 0, 130, 244, 155, 12);
    dialog_control(dialog, L"STATIC", L"Left / up / lower", 0, 0, 295, 244, 145, 12);
    dialog_control(dialog, L"STATIC", L"Right / down / higher", 0, 0, 450, 244, 145, 12);
    for (unsigned n = 0; n < axis_names.size(); ++n) {
      const int y = 260 + int(n) * 21;
      dialog_control(dialog, L"STATIC", axis_names[n].data(), 0, 0, 10, y + 3, 115, 12);
      const auto axis =
          dialog_control(dialog, L"COMBOBOX", L"", dropdown, axes_first + n, 130, y, 155, 180);
      for (const auto name : {L"None", L"Left stick X", L"Left stick Y", L"Right stick X",
                              L"Right stick Y", L"Left trigger", L"Right trigger"})
        SendMessageW(axis, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
      dialog_control(dialog, L"COMBOBOX", L"", dropdown, negative_first + n, 295, y, 145, 180);
      dialog_control(dialog, L"COMBOBOX", L"", dropdown, positive_first + n, 450, y, 145, 180);
      dialog_control(dialog, L"BUTTON", L"Invert", BS_AUTOCHECKBOX | WS_TABSTOP, inverted_first + n,
                     610, y, 60, 16);
    }
    dialog_control(dialog, L"STATIC", L"Walk / slow stick:", 0, 0, 10, 400, 105, 12);
    dialog_control(dialog, L"COMBOBOX", L"", dropdown, walk_id, 120, 397, 210, 180);
    dialog_control(dialog, L"BUTTON", L"Rumble feedback", BS_AUTOCHECKBOX | WS_TABSTOP, feedback_id,
                   350, 397, 145, 16);
    dialog_control(dialog, L"BUTTON", L"Restore defaults", BS_PUSHBUTTON | WS_TABSTOP, defaults_id,
                   10, 430, 100, 16);
    dialog_control(dialog, L"BUTTON", L"Clear bindings", BS_PUSHBUTTON | WS_TABSTOP, clear_id, 120,
                   430, 100, 16);
    dialog_control(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 560, 430, 50, 16);
    dialog_control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 620, 430, 50,
                   16);
    refresh(dialog, *state);
    try {
      state->layout.initialize(dialog, 420);
    } catch (const std::exception &error) {
      MessageBoxA(dialog, error.what(), "Input mappings", MB_OK | MB_ICONERROR);
      EndDialog(dialog, IDCANCEL);
      return TRUE;
    }
    center_dialog(dialog);
    SetTimer(dialog, 20, 1000, nullptr);
    return TRUE;
  }
  if (!state)
    return FALSE;
  if (state->layout.message(event, wparam))
    return TRUE;
  const auto controls = state->layout.content();
  if (event == WM_TIMER && wparam == 20) {
    status(controls, *state);
    return TRUE;
  }
  if (event != WM_COMMAND)
    return FALSE;
  const auto id = LOWORD(wparam);
  if (id == IDCANCEL) {
    EndDialog(dialog, IDCANCEL);
    return TRUE;
  }
  try {
    if (id == IDOK) {
      read(controls, *state);
      for (const auto &mapping : state->form.mappings)
        validate_mapping(mapping);
      EndDialog(dialog, IDOK);
    } else if (id == player_id && HIWORD(wparam) == CBN_SELCHANGE) {
      read(controls, *state);
      state->player =
          static_cast<unsigned>(SendDlgItemMessageW(controls, player_id, CB_GETCURSEL, 0, 0));
      refresh(controls, *state);
    } else if (id == source_id && HIWORD(wparam) == CBN_SELCHANGE) {
      read(controls, *state);
      auto &mapping = state->form.mappings[state->player];
      const auto source =
          static_cast<HostSource>(SendDlgItemMessageW(controls, source_id, CB_GETCURSEL, 0, 0));
      if ((source >= HostSource::Gamepad1) != (mapping.source >= HostSource::Gamepad1)) {
        const bool feedback = mapping.feedback;
        mapping = default_mapping(source);
        mapping.feedback = feedback;
      } else {
        mapping.source = source;
      }
      refresh(controls, *state);
    } else if (id == defaults_id || id == clear_id) {
      read(controls, *state);
      auto &mapping = state->form.mappings[state->player];
      if (id == defaults_id)
        mapping = default_mapping(mapping.source);
      else {
        mapping.buttons.fill({});
        mapping.axes.fill({});
        mapping.slow = {};
      }
      refresh(controls, *state);
    }
  } catch (const std::exception &error) {
    MessageBoxA(dialog, error.what(), "Input mappings", MB_OK | MB_ICONERROR);
  }
  return TRUE;
}

} // namespace

std::optional<PlayerMappings> choose_mappings(HWND owner, MappingForm form) {
  DialogState state{std::move(form), 0, {}, {}};
  const auto data = dialog_template(L"Input mappings for four players", 680, 456);
  const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                              reinterpret_cast<const DLGTEMPLATE *>(data.data()),
                                              owner, procedure, reinterpret_cast<LPARAM>(&state));
  if (result == -1)
    throw std::runtime_error("Could not open input mapping settings.");
  return result == IDOK ? std::optional(std::move(state.form.mappings)) : std::nullopt;
}

} // namespace cupid::desktop
