#include "core/arcade/aleck64.hpp"
#include <bit>

namespace cupid::n64 {

std::span<const DipSetting> Aleck64::dip_settings() const {
  return arcade_dip_settings(profile_);
}

void Aleck64::apply_dip(const DipSetting &setting, const DipValue &value) {
  if (!value.bits)
    return;
  auto &bank = dip_switches_[setting.bank];
  bank = static_cast<std::uint8_t>(
      (bank & ~setting.mask) |
      ((*value.bits << std::countr_zero(unsigned(setting.mask))) & setting.mask));
}

void Aleck64::reset_dip_switches() {
  dip_switches_ = {255, 255};
  const auto settings = dip_settings();
  if (dip_values_.size() != settings.size()) {
    dip_values_.clear();
    for (const auto &setting : settings)
      dip_values_.push_back({setting.default_value, setting.initial_latch.empty()
                                                        ? setting.default_value
                                                        : setting.initial_latch});
  }
  for (const auto &setting : settings)
    for (const auto &choice : setting.choices)
      if (choice.label == setting.default_value)
        apply_dip(setting, choice);
}

std::optional<std::string_view> Aleck64::dip_setting(std::string_view name) const {
  const auto settings = dip_settings();
  for (unsigned index = 0; index < settings.size(); ++index)
    if (settings[index].name == name)
      return dip_values_[index].current;
  return {};
}

bool Aleck64::dip_setting(std::string_view name, std::string_view value) {
  const auto settings = dip_settings();
  for (unsigned index = 0; index < settings.size(); ++index) {
    const auto &setting = settings[index];
    if (setting.name != name)
      continue;
    for (const auto &choice : setting.choices) {
      if (choice.label != value)
        continue;
      auto &state = dip_values_[index];
      state.current = choice.label;
      if (state.latched != choice.label) {
        state.latched = choice.label;
        apply_dip(setting, choice);
      }
      return true;
    }
    return false;
  }
  return false;
}

bool Aleck64::dip_setting(std::string_view name, bool value) {
  for (const auto &setting : dip_settings())
    if (setting.name == name && setting.boolean)
      return dip_setting(name, value ? std::string_view("true") : std::string_view("false"));
  return false;
}

} // namespace cupid::n64
