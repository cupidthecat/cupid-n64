#include "../fixture.hpp"
#include "core/arcade/aleck64.hpp"
#include "core/state/core_state.hpp"
#include "core/system/console.hpp"
#include "dips/expected.hpp"
#include <charconv>
#include <memory>

namespace test {
using namespace cupid::n64;

void arcade_dip_tests() {
  std::unique_ptr<Aleck64> board;
  unsigned setting_index = 0, lines = 0;
  auto data = arcade_dips::expected;
  while (!data.empty()) {
    const auto end = data.find('\n');
    auto line = data.substr(0, end);
    data.remove_prefix(end == data.npos ? data.size() : end + 1);
    ++lines;
    std::vector<std::string_view> fields;
    while (true) {
      const auto separator = line.find('|');
      fields.push_back(line.substr(0, separator));
      if (separator == line.npos)
        break;
      line.remove_prefix(separator + 1);
    }
    if (fields[1] == "default") {
      if (board)
        equal(setting_index, board->dip_settings().size());
      const auto profile = arcade_profile(fields[0]);
      equal(profile.has_value(), true);
      if (!profile)
        return;
      board = std::make_unique<Aleck64>(*profile);
      setting_index = 0;
    }
    if (fields[1] == "setting") {
      const auto settings = board->dip_settings();
      equal(setting_index < settings.size(), true);
      if (setting_index >= settings.size())
        return;
      const auto &setting = settings[setting_index++];
      equal(setting.name == fields[2], true);
      equal(board->dip_setting(fields[2]).value_or("") == fields[3], true);
      equal((setting.initial_latch.empty() ? setting.default_value : setting.initial_latch) ==
                fields[4],
            true);
      equal(setting.choices.size(), fields.size() - 5);
      for (unsigned n = 0; n < setting.choices.size() && n + 5 < fields.size(); ++n)
        equal(setting.choices[n].label == fields[n + 5], true);
      continue;
    }
    if (fields[1] == "set" || fields[1] == "reverse" || fields[1] == "repeat") {
      board->dip_switches() = fields[1] == "repeat"    ? std::array<std::uint8_t, 2>{0x3c, 0xc3}
                              : fields[1] == "reverse" ? std::array<std::uint8_t, 2>{0x5a, 0xa5}
                                                       : std::array<std::uint8_t, 2>{0xa5, 0x5a};
      equal(board->dip_setting(fields[2], fields[3]), true);
      equal(board->dip_setting(fields[2]).value_or("") == fields[3], true);
    }
    if (fields[1] == "reset")
      board->power(true);
    if (fields[1] == "power")
      board->power(false);
    if (fields[1] == "retained")
      equal(board->dip_setting(fields[2]).value_or("") == fields[3], true);
    std::uint32_t expected = 0;
    const auto result = std::from_chars(fields.back().data(),
                                        fields.back().data() + fields.back().size(), expected, 16);
    equal(result.ec == std::errc{}, true);
    equal(board->read(0xc0800000, 4).value, expected);
  }
  equal(lines, 621);
  equal(setting_index, board->dip_settings().size());

  Aleck64 soldier(ArcadeProfile::StarSoldier);
  const auto banks = soldier.dip_switches();
  equal(soldier.dip_setting("No Such Setting", "true"), false);
  equal(soldier.dip_setting("Joystick Type", true), false);
  equal(soldier.dip_setting("Joystick Type", "Not Listed"), false);
  equal(soldier.dip_switches() == banks, true);
  equal(soldier.dip_setting("Test Mode", true), true);
  equal(soldier.dip_switches()[1] & 0x80, 0);

  ConsoleConfig config;
  config.arcade_profile = ArcadeProfile::VividDolls;
  config.random_seed = 0;
  auto selected = std::make_unique<Console>(config);
  auto *arcade = selected->arcade();
  equal(arcade->dip_setting("Controls", "JAMMASTICK"), true);
  const auto snapshot = CoreState::capture(*selected);
  equal(arcade->dip_setting("Controls", "JOYSTICK"), true);
  CoreState::restore(*selected, snapshot);
  equal(arcade->dip_switches()[1] & 0x20, 0);
  equal(arcade->dip_setting("Controls").value_or("") == "JOYSTICK", true);
  equal(arcade->dip_setting("Controls", "JOYSTICK"), true);
  equal(arcade->dip_switches()[1] & 0x20, 0);
  config.arcade_profile = ArcadeProfile::Standard;
  auto legacy = std::make_unique<Console>(config);
  CoreState::restore(*legacy, snapshot);
  equal(legacy->arcade()->dip_switches()[1] & 0x20, 0);
  equal(CoreState::capture(*legacy) == snapshot, true);
}

} // namespace test
