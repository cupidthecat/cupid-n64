#include "desktop/linux/settings/settings.hpp"
#include "desktop/linux/text.hpp"
#include "desktop/storage/save.hpp"
#include <algorithm>
#include <glib.h>
#include <memory>
#include <stdexcept>

namespace cupid::desktop::linux_host {
namespace {

class Document {
public:
  Document() : file_(g_key_file_new(), g_key_file_unref) {}
  void load(std::span<const std::uint8_t> bytes) {
    GError *error = nullptr;
    if (!g_key_file_load_from_data(file_.get(), reinterpret_cast<const char *>(bytes.data()),
                                   bytes.size(), G_KEY_FILE_NONE, &error))
      fail(error);
  }
  unsigned number(const char *group, const char *key, unsigned fallback) const {
    if (!g_key_file_has_key(file_.get(), group, key, nullptr))
      return fallback;
    GError *error = nullptr;
    const auto value = g_key_file_get_integer(file_.get(), group, key, &error);
    if (error)
      fail(error);
    if (value < 0)
      throw std::runtime_error("Settings cannot contain a negative count.");
    return static_cast<unsigned>(value);
  }
  int signed_number(const char *group, const char *key, int fallback) const {
    if (!g_key_file_has_key(file_.get(), group, key, nullptr))
      return fallback;
    GError *error = nullptr;
    const auto value = g_key_file_get_integer(file_.get(), group, key, &error);
    if (error)
      fail(error);
    return value;
  }
  bool flag(const char *group, const char *key, bool fallback) const {
    if (!g_key_file_has_key(file_.get(), group, key, nullptr))
      return fallback;
    GError *error = nullptr;
    const bool value = g_key_file_get_boolean(file_.get(), group, key, &error);
    if (error)
      fail(error);
    return value;
  }
  std::string text(const char *group, const char *key) const {
    if (!g_key_file_has_key(file_.get(), group, key, nullptr))
      return {};
    GError *error = nullptr;
    auto value = g_key_file_get_string(file_.get(), group, key, &error);
    if (error)
      fail(error);
    std::string result(value);
    g_free(value);
    return result;
  }
  void number(const char *group, const char *key, int value) {
    g_key_file_set_integer(file_.get(), group, key, value);
  }
  void flag(const char *group, const char *key, bool value) {
    g_key_file_set_boolean(file_.get(), group, key, value);
  }
  void text(const char *group, const char *key, const std::string &value) {
    g_key_file_set_string(file_.get(), group, key, value.c_str());
  }
  std::vector<std::uint8_t> bytes() const {
    gsize size = 0;
    auto data = g_key_file_to_data(file_.get(), &size, nullptr);
    std::vector<std::uint8_t> result(data, data + size);
    g_free(data);
    return result;
  }

private:
  [[noreturn]] static void fail(GError *error) {
    const std::string text = error ? error->message : "Could not read the settings file.";
    if (error)
      g_error_free(error);
    throw std::runtime_error(text);
  }
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file_;
};

InputBinding binding(const Document &document, const char *group, const std::string &key,
                     InputBinding fallback) {
  return {static_cast<BindingKind>(
              document.number(group, (key + "Kind").c_str(), static_cast<unsigned>(fallback.kind))),
          document.number(group, (key + "Code").c_str(), fallback.code)};
}

void binding(Document &document, const char *group, const std::string &key, InputBinding value) {
  document.number(group, (key + "Kind").c_str(), static_cast<int>(value.kind));
  document.number(group, (key + "Code").c_str(), static_cast<int>(value.code));
}

} // namespace

void Settings::validate() const {
  if (!hardware && session.graphics != n64::RenderSettings{})
    throw std::runtime_error("Rendering resolution and VI options require hardware rendering.");
  validate_ports(session.ports);
  for (const auto &mapping : mappings)
    validate_mapping(mapping);
  for (const auto banks : session.memory.pak_banks)
    if (std::find(MemorySettings::capacities.begin(), MemorySettings::capacities.end(), banks) ==
        MemorySettings::capacities.end())
      throw std::runtime_error("Select a supported Controller Pak capacity.");
  if (!session.graphics.valid() || audio.levels.volume > 200 || audio.levels.balance < -100 ||
      audio.levels.balance > 100 || audio.latency < 20 || audio.latency > 320 ||
      audio.latency % 10 || scale > DisplayScale::Stretch || aspect > DisplayAspect::Pixels ||
      sync > PlaybackSync::Video || focus > FocusBehavior::AllowInput || !gdb_port ||
      gdb_port > 65535 || width < 320 || width > 8192 || height < 240 || height > 8192)
    throw std::runtime_error("Select a supported audio, video, or playback setting.");
}

std::filesystem::path settings_path() {
  return std::filesystem::path(g_get_user_config_dir()) / "cupid-n64" / "settings.ini";
}

Settings load_settings(const std::filesystem::path &path) {
  Settings settings;
  if (!std::filesystem::exists(path))
    return settings;
  Document loaded;
  loaded.load(read_file(path, 1024 * 1024));
  const auto &document = loaded;
  settings.session.memory.expansion = document.flag("Memory", "Expansion", true);
  settings.audio.levels.volume = document.number("Audio", "Volume", 100);
  settings.audio.levels.balance = document.signed_number("Audio", "Balance", 0);
  settings.audio.latency = document.number("Audio", "Latency", 40);
  settings.audio.device = wide(document.text("Audio", "Device"));
  settings.muted = document.flag("Audio", "Muted", false);
  settings.feedback = document.flag("Input", "Feedback", true);
  settings.hardware = document.flag("Video", "Hardware", true);
  settings.session.graphics.scale = document.number("Video", "RenderScale", 1);
  settings.session.graphics.supersampling = document.flag("Video", "Supersampling", false);
  settings.session.graphics.weave = document.flag("Video", "Weave", false);
  settings.session.graphics.bypass_vi = document.flag("Video", "BypassVi", false);
  settings.scale = static_cast<DisplayScale>(document.number("Video", "Scale", 0));
  settings.aspect = static_cast<DisplayAspect>(document.number("Video", "Aspect", 0));
  settings.width = document.number("Video", "Width", 960);
  settings.height = document.number("Video", "Height", 720);
  settings.sync = static_cast<PlaybackSync>(document.number("Playback", "Sync", 0));
  settings.focus = static_cast<FocusBehavior>(document.number("Playback", "Focus", 0));
  settings.gdb_enabled = document.flag("Debugger", "Enabled", false);
  settings.gdb_port = document.number("Debugger", "Port", 2345);
  settings.firmware.n64 = document.text("Firmware", "Nintendo64");
  settings.firmware.arcade = document.text("Firmware", "Aleck64");
  settings.firmware.ipl = document.text("Firmware", "DiskIpl");
  settings.session.storage.saves = document.text("Storage", "Saves");
  settings.session.storage.screenshots = document.text("Storage", "Screenshots");
  settings.session.storage.firmware = document.text("Storage", "Firmware");
  settings.session.storage.debugging = document.text("Storage", "Debugging");
  for (unsigned port = 0; port < settings.mappings.size(); ++port) {
    const auto group = "Port" + std::to_string(port + 1);
    const auto name = group.c_str();
    auto &configured = settings.session.ports[port];
    configured.controller = static_cast<ControllerType>(document.number(name, "Controller", 0));
    configured.accessory = static_cast<AccessoryType>(document.number(name, "Accessory", 0));
    configured.cartridge = document.text(name, "Cartridge");
    configured.heart_rate = document.number(name, "HeartRate", 60);
    settings.session.memory.pak_banks[port] = document.number(name, "PakBanks", 1);
    auto &mapping = settings.mappings[port];
    mapping = default_mapping(static_cast<HostSource>(
        document.number(name, "Source", static_cast<unsigned>(mapping.source))));
    mapping.feedback = document.flag(name, "Feedback", true);
    mapping.slow = binding(document, name, "Slow", mapping.slow);
    for (unsigned n = 0; n < mapping.buttons.size(); ++n)
      mapping.buttons[n] =
          binding(document, name, "Button" + std::to_string(n), mapping.buttons[n]);
    for (unsigned n = 0; n < mapping.axes.size(); ++n) {
      auto &axis = mapping.axes[n];
      const auto key = "Axis" + std::to_string(n);
      axis.source = static_cast<HostAxis>(
          document.number(name, (key + "Source").c_str(), static_cast<unsigned>(axis.source)));
      axis.inverted = document.flag(name, (key + "Inverted").c_str(), false);
      axis.negative = binding(document, name, key + "Negative", axis.negative);
      axis.positive = binding(document, name, key + "Positive", axis.positive);
    }
  }
  settings.validate();
  return settings;
}

void save_settings(const std::filesystem::path &path, const Settings &settings) {
  settings.validate();
  Document document;
  document.flag("Memory", "Expansion", settings.session.memory.expansion);
  document.number("Audio", "Volume", static_cast<int>(settings.audio.levels.volume));
  document.number("Audio", "Balance", settings.audio.levels.balance);
  document.number("Audio", "Latency", static_cast<int>(settings.audio.latency));
  document.text("Audio", "Device", utf8(settings.audio.device));
  document.flag("Audio", "Muted", settings.muted);
  document.flag("Input", "Feedback", settings.feedback);
  document.flag("Video", "Hardware", settings.hardware);
  document.number("Video", "RenderScale", static_cast<int>(settings.session.graphics.scale));
  document.flag("Video", "Supersampling", settings.session.graphics.supersampling);
  document.flag("Video", "Weave", settings.session.graphics.weave);
  document.flag("Video", "BypassVi", settings.session.graphics.bypass_vi);
  document.number("Video", "Scale", static_cast<int>(settings.scale));
  document.number("Video", "Aspect", static_cast<int>(settings.aspect));
  document.number("Video", "Width", static_cast<int>(settings.width));
  document.number("Video", "Height", static_cast<int>(settings.height));
  document.number("Playback", "Sync", static_cast<int>(settings.sync));
  document.number("Playback", "Focus", static_cast<int>(settings.focus));
  document.flag("Debugger", "Enabled", settings.gdb_enabled);
  document.number("Debugger", "Port", static_cast<int>(settings.gdb_port));
  document.text("Firmware", "Nintendo64", settings.firmware.n64.native());
  document.text("Firmware", "Aleck64", settings.firmware.arcade.native());
  document.text("Firmware", "DiskIpl", settings.firmware.ipl.native());
  document.text("Storage", "Saves", settings.session.storage.saves.native());
  document.text("Storage", "Screenshots", settings.session.storage.screenshots.native());
  document.text("Storage", "Firmware", settings.session.storage.firmware.native());
  document.text("Storage", "Debugging", settings.session.storage.debugging.native());
  for (unsigned port = 0; port < settings.mappings.size(); ++port) {
    const auto group = "Port" + std::to_string(port + 1);
    const auto name = group.c_str();
    const auto &configured = settings.session.ports[port];
    document.number(name, "Controller", static_cast<int>(configured.controller));
    document.number(name, "Accessory", static_cast<int>(configured.accessory));
    document.text(name, "Cartridge", configured.cartridge.native());
    document.number(name, "HeartRate", static_cast<int>(configured.heart_rate));
    document.number(name, "PakBanks", static_cast<int>(settings.session.memory.pak_banks[port]));
    const auto &mapping = settings.mappings[port];
    document.number(name, "Source", static_cast<int>(mapping.source));
    document.flag(name, "Feedback", mapping.feedback);
    binding(document, name, "Slow", mapping.slow);
    for (unsigned n = 0; n < mapping.buttons.size(); ++n)
      binding(document, name, "Button" + std::to_string(n), mapping.buttons[n]);
    for (unsigned n = 0; n < mapping.axes.size(); ++n) {
      const auto &axis = mapping.axes[n];
      const auto key = "Axis" + std::to_string(n);
      document.number(name, (key + "Source").c_str(), static_cast<int>(axis.source));
      document.flag(name, (key + "Inverted").c_str(), axis.inverted);
      binding(document, name, key + "Negative", axis.negative);
      binding(document, name, key + "Positive", axis.positive);
    }
  }
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  write_save(path, document.bytes());
}

} // namespace cupid::desktop::linux_host
