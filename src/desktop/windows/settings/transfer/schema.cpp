#include "desktop/windows/settings/transfer/schema.hpp"
#include "core/arcade/aleck64.hpp"
#include "desktop/controls/mapping.hpp"
#include "desktop/controls/shortcuts/bindings.hpp"
#include "renderer/video/settings.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

namespace cupid::desktop::settings {
namespace {

struct Rule {
  std::wstring fallback;
  bool number = false;
  int minimum = 0, maximum = 0;
  std::vector<int> choices;
  std::optional<PathKind> path;
  std::size_t length = 32760;
  bool optional = false;
};

using Rules = std::map<std::wstring, Rule, NameLess>;
using Schema = std::map<std::wstring, Rules, NameLess>;

bool same(std::wstring_view first, std::wstring_view second) {
  return !NameLess{}(first, second) && !NameLess{}(second, first);
}

bool controller_section(std::wstring_view name) {
  constexpr std::wstring_view prefix = L"Controller ports ";
  return name.size() == prefix.size() + 16 && same(name.substr(0, prefix.size()), prefix) &&
         name.substr(prefix.size()).find_first_not_of(L"0123456789abcdefABCDEF") == name.npos;
}

void number(Rules &rules, std::wstring name, int fallback, int minimum, int maximum) {
  rules.emplace(std::move(name),
                Rule{std::to_wstring(fallback), true, minimum, maximum, {}, {}, 16});
}

void choice(Rules &rules, std::wstring name, int fallback, std::vector<int> choices) {
  const auto [minimum, maximum] = std::minmax_element(choices.begin(), choices.end());
  number(rules, name, fallback, *minimum, *maximum);
  rules.at(name).choices = std::move(choices);
}

void path(Rules &rules, std::wstring name, PathKind kind) {
  Rule rule;
  rule.path = kind;
  rules.emplace(std::move(name), std::move(rule));
}

std::wstring port_key(unsigned port, std::wstring_view name) {
  return L"Port " + std::to_wstring(port + 1) + L" " + std::wstring(name);
}

void binding(Rules &rules, const std::wstring &name, InputBinding fallback) {
  number(rules, name + L" Kind", static_cast<int>(fallback.kind), 0, 4);
  number(rules, name + L" Code", static_cast<int>(fallback.code), 0, 65535);
}

Schema schema(const Document &document) {
  Schema result;
  auto &display = result[L"Display"];
  number(display, L"Scale", 0, 0, 2);
  number(display, L"Aspect", 0, 0, 1);
  number(display, L"Width", 960, 64, 8192);
  number(display, L"Height", 720, 64, 8192);
  display[L"FullscreenMonitor"].length = 255;
  auto &audio = result[L"Audio"];
  number(audio, L"Volume", 100, 0, 200);
  number(audio, L"Balance", 0, -100, 100);
  choice(audio, L"Latency", 80, {20, 40, 60, 80, 120, 160, 240, 320});
  audio[L"Device"] = {};
  number(result[L"Playback"], L"Sync", 0, 0, 1);
  number(result[L"Playback"], L"Focus", 0, 0, 2);
  auto &rewind = result[L"Rewind"];
  number(rewind, L"Enabled", 0, 0, 1);
  number(rewind, L"Mute", 1, 0, 1);
  choice(rewind, L"Interval", 60, {20, 40, 60, 80, 100, 120});
  choice(rewind, L"Capacity", 10, {10, 20, 40, 80, 160, 320});
  choice(rewind, L"MemoryMB", 256, {256, 512, 1024});
  number(result[L"States"], L"Slot", 1, 1, 10);
  auto &developer = result[L"Developer"];
  number(developer, L"GDB enabled", 0, 0, 1);
  number(developer, L"GDB port", 2345, 1, 65535);
  for (auto &[key, rule] : developer) {
    (void)key;
    rule.optional = true;
  }
  auto &graphics = result[L"Graphics"];
  choice(graphics, L"InternalScale", 1, {1, 2, 4, 8});
  for (const auto key : {L"Supersampling", L"Weave", L"BypassVI"})
    number(graphics, key, 0, 0, 1);
  number(result[L"Shader"], L"Preset", 0, 0, 3);
  path(result[L"Shader"], L"File", PathKind::Shader);
  path(result[L"Firmware"], L"Nintendo64", PathKind::Pif);
  path(result[L"Firmware"], L"Aleck64", PathKind::Pif);
  path(result[L"Firmware"], L"64DD IPL", PathKind::Ipl);
  for (const auto key : {L"Saves", L"Screenshots", L"Firmware", L"Debugging"})
    path(result[L"Folders"], key, PathKind::Folder);
  path(result[L"Launch"], L"ROM", PathKind::Cartridge);
  path(result[L"Launch"], L"IPL", PathKind::Ipl);
  path(result[L"Launch"], L"Disk", PathKind::Disk);
  number(result[L"Launch"], L"Arcade", 0, 0, static_cast<int>(n64::ArcadeProfile::TowerShaft));
  path(result[L"Files"], L"ROM", PathKind::Cartridge);
  path(result[L"Files"], L"Firmware", PathKind::Pif);
  for (unsigned index = 1; index <= 10; ++index)
    path(result[L"Recent ROMs"], std::to_wstring(index), PathKind::Cartridge);
  number(result[L"Memory"], L"Expansion Pak", 1, 0, 1);
  for (unsigned port = 0; port < 4; ++port) {
    choice(result[L"Memory"], L"Port " + std::to_wstring(port + 1), 1, {1, 4, 16, 62});
    const auto section = L"Input mapping " + std::to_wstring(port + 1);
    auto source = port == 0 ? 1 : 0;
    if (const auto found = document.find(section); found != document.end())
      if (const auto selected = found->second.find(L"Source"); selected != found->second.end())
        source = integer(selected->second);
    if (source < 0 || source > 5)
      throw std::runtime_error("An imported input mapping selects an unsupported host device.");
    const auto defaults = default_mapping(static_cast<HostSource>(source));
    auto &mapping = result[section];
    number(mapping, L"Source", port == 0 ? 1 : 0, 0, 5);
    number(mapping, L"Feedback", 1, 0, 1);
    for (unsigned index = 0; index < defaults.buttons.size(); ++index)
      binding(mapping, L"Button " + std::to_wstring(index), defaults.buttons[index]);
    for (unsigned index = 0; index < defaults.axes.size(); ++index) {
      const auto key = L"Axis " + std::to_wstring(index);
      const auto &axis = defaults.axes[index];
      number(mapping, key, static_cast<int>(axis.source), 0, 6);
      number(mapping, key + L" Inverted", 0, 0, 1);
      binding(mapping, key + L" Negative", axis.negative);
      binding(mapping, key + L" Positive", axis.positive);
    }
    binding(mapping, L"Walk", defaults.slow);
  }
  const auto shortcuts = default_shortcuts();
  for (unsigned action = 0; action < shortcuts.size(); ++action)
    for (unsigned slot = 0; slot < shortcuts[action].size(); ++slot) {
      const auto key = L"Action " + std::to_wstring(action) + L" Binding " + std::to_wstring(slot);
      number(result[L"Shortcuts"], key + L" Key", static_cast<int>(shortcuts[action][slot].key), 0,
             255);
      number(result[L"Shortcuts"], key + L" Modifiers",
             static_cast<int>(shortcuts[action][slot].modifiers), 0, 7);
    }
  for (const auto &[section, entries] : document) {
    (void)entries;
    if (!controller_section(section))
      continue;
    auto &ports = result[section];
    for (unsigned port = 0; port < 4; ++port) {
      number(ports, port_key(port, L"Controller"), 0, 0, 4);
      number(ports, port_key(port, L"Accessory"), 0, 0, 5);
      number(ports, port_key(port, L"Heart rate"), 60, 30, 180);
      path(ports, port_key(port, L"Cartridge"), PathKind::Handheld);
    }
  }
  return result;
}

void validate(const std::wstring &value, const Rule &rule) {
  if (value.size() > rule.length || value.find_first_of(L"\r\n\t") != value.npos ||
      value.find(L'\0') != value.npos)
    throw std::runtime_error("An imported setting contains an invalid or oversized value.");
  if (rule.number) {
    const auto number = integer(value);
    if (number < rule.minimum || number > rule.maximum ||
        (!rule.choices.empty() &&
         std::find(rule.choices.begin(), rule.choices.end(), number) == rule.choices.end()))
      throw std::runtime_error("An imported setting is outside its supported range.");
  }
}

void validate_combinations(const Document &document) {
  const auto number = [&](const wchar_t *section, const wchar_t *key) {
    return integer(document.at(section).at(key));
  };
  const n64::RenderSettings graphics{static_cast<unsigned>(number(L"Graphics", L"InternalScale")),
                                     number(L"Graphics", L"Supersampling") != 0,
                                     number(L"Graphics", L"Weave") != 0,
                                     number(L"Graphics", L"BypassVI") != 0};
  if (!graphics.valid())
    throw std::runtime_error(
        "Imported supersampling requires 2x/4x/8x resolution and excludes weave.");
  if (number(L"Shader", L"Preset") == 3 && document.at(L"Shader").at(L"File").empty())
    throw std::runtime_error("The imported custom display shader has no file path.");
  for (unsigned port = 0; port < 4; ++port) {
    const auto &section = document.at(L"Input mapping " + std::to_wstring(port + 1));
    const auto binding = [&](const std::wstring &key) {
      return InputBinding{static_cast<BindingKind>(integer(section.at(key + L" Kind"))),
                          static_cast<unsigned>(integer(section.at(key + L" Code")))};
    };
    ControllerMapping mapping;
    mapping.source = static_cast<HostSource>(integer(section.at(L"Source")));
    mapping.slow = binding(L"Walk");
    for (unsigned index = 0; index < mapping.buttons.size(); ++index)
      mapping.buttons[index] = binding(L"Button " + std::to_wstring(index));
    for (unsigned index = 0; index < mapping.axes.size(); ++index) {
      const auto key = L"Axis " + std::to_wstring(index);
      mapping.axes[index] = {static_cast<HostAxis>(integer(section.at(key))),
                             integer(section.at(key + L" Inverted")) != 0,
                             binding(key + L" Negative"), binding(key + L" Positive")};
    }
    validate_mapping(mapping);
  }
  ShortcutBindings shortcuts;
  for (unsigned action = 0; action < shortcuts.size(); ++action)
    for (unsigned slot = 0; slot < shortcuts[action].size(); ++slot) {
      const auto key = L"Action " + std::to_wstring(action) + L" Binding " + std::to_wstring(slot);
      shortcuts[action][slot] = {
          static_cast<unsigned>(integer(document.at(L"Shortcuts").at(key + L" Key"))),
          static_cast<unsigned>(integer(document.at(L"Shortcuts").at(key + L" Modifiers")))};
    }
  validate_shortcuts(shortcuts);
}

} // namespace

int integer(std::wstring_view text) {
  const bool negative = !text.empty() && text.front() == L'-';
  if (negative)
    text.remove_prefix(1);
  if (text.empty())
    throw std::runtime_error("An imported numeric setting is empty.");
  unsigned value = 0;
  for (const auto character : text) {
    if (character < L'0' || character > L'9' ||
        value >
            (static_cast<unsigned>(std::numeric_limits<int>::max()) - unsigned(character - L'0')) /
                10)
      throw std::runtime_error("An imported numeric setting is invalid.");
    value = value * 10 + unsigned(character - L'0');
  }
  return negative ? -static_cast<int>(value) : static_cast<int>(value);
}

Document normalize(const Document &document, bool complete) {
  const auto rules = schema(document);
  Document result;
  for (const auto &[section, entries] : rules) {
    const auto found = document.find(section);
    for (const auto &[key, rule] : entries) {
      const auto selected = found == document.end() ? nullptr : [&]() -> const std::wstring * {
        const auto entry = found->second.find(key);
        return entry == found->second.end() ? nullptr : &entry->second;
      }();
      if (!selected && complete && !rule.optional)
        throw std::runtime_error("The settings export is incomplete.");
      auto value = selected ? *selected : rule.fallback;
      validate(value, rule);
      if (rule.number)
        value = std::to_wstring(integer(value));
      result[section].emplace(key, std::move(value));
    }
  }
  if (complete)
    for (const auto &[section, entries] : document) {
      const auto found = rules.find(section);
      if (found == rules.end())
        throw std::runtime_error("The export contains an unsupported settings section.");
      for (const auto &[key, value] : entries) {
        (void)value;
        if (!found->second.contains(key))
          throw std::runtime_error("The export contains an unsupported setting.");
      }
    }
  if (!complete) {
    if (const auto firmware = document.find(L"Firmware");
        firmware == document.end() || !firmware->second.contains(L"Nintendo64"))
      result[L"Firmware"][L"Nintendo64"] = result.at(L"Files").at(L"Firmware");
    if (result.at(L"Launch").at(L"ROM").empty() && result.at(L"Launch").at(L"IPL").empty())
      result[L"Launch"][L"ROM"] = result.at(L"Files").at(L"ROM");
  }
  validate_combinations(result);
  return result;
}

bool supported_section(std::wstring_view section) {
  static const auto rules = schema({});
  return controller_section(section) || rules.contains(std::wstring(section));
}

std::vector<LocalPath> local_paths(const Document &document) {
  std::vector<LocalPath> result;
  for (const auto &[section, entries] : schema(document))
    for (const auto &[key, rule] : entries)
      if (rule.path && !document.at(section).at(key).empty())
        result.push_back({section, key, document.at(section).at(key), *rule.path});
  return result;
}

} // namespace cupid::desktop::settings
