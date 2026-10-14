#include "core/arcade/aleck64.hpp"
#include <array>

namespace cupid::n64 {
namespace {

constexpr std::array<DipValue, 2> low{{{"false", 1}, {"true", 0}}};
constexpr std::array<DipValue, 2> high{{{"false", 0}, {"true", 1}}};
constexpr DipSetting test1{"Test Mode", "false", 0, 1, low, true};
constexpr DipSetting test2{"Test Mode", "false", 1, 128, low, true};
constexpr std::array<DipSetting, 1> first_test{test1}, second_test{test2};
constexpr std::array<DipValue, 2> joystick{{{"2D", 0}, {"3D", 1}}};
constexpr std::array<DipValue, 4> players{{{"4", 2}, {"3", 3}, {"2", 1}, {"1", 0}}};
constexpr std::array<DipValue, 4> auto_level{
    {{"Normal", 3}, {"Slow", 2}, {"Fast1", 1}, {"Fast2", 0}}};
constexpr std::array<DipValue, 8> soldier_coins{{{"1 Coin 1 Credit", 7},
                                                 {"1 Coin 2 Credits", 6},
                                                 {"1 Coin 3 Credits", 5},
                                                 {"1 Coin 4 Credits", 4},
                                                 {"2 Coins 1 Credit", 3},
                                                 {"3 Coins 1 Credit", 2},
                                                 {"4 Coins 1 Credit", 1},
                                                 {"5 Coins 1 Credit", 0}}};
constexpr std::array<DipValue, 2> language{{{"English", 1}, {"Japanese", 0}}};
constexpr std::array<DipValue, 4> extend{
    {{"30,000,000", 3}, {"50,000,000", 2}, {"70,000,000", 1}, {"None", 0}}};
constexpr std::array<DipValue, 4> soldier_difficulty{
    {{"Normal", 3}, {"Easy", 2}, {"Hard1", 1}, {"Hard2", 0}}};
constexpr std::array<DipSetting, 10> soldier{
    {test2,
     {"Joystick Type", "2D", 0, 128, joystick},
     {"Player", "3", 0, 24, players},
     {"Auto Level", "Normal", 0, 96, auto_level},
     {"Coinage", "1 Coin 1 Credit", 0, 7, soldier_coins},
     {"Language", "English", 1, 64, language},
     {"Demo Sound", "true", 1, 32, low, true},
     {"Rapid", "false", 1, 16, high, true},
     {"Extend", "30,000,000", 1, 12, extend, false, "30000000"},
     {"Difficulty", "Normal", 1, 3, soldier_difficulty}}};
constexpr std::array<DipValue, 8> mahjong_coins{{{"1 Coin 1 Credit", 7},
                                                 {"1 Coin 2 Credits", 3},
                                                 {"1 Coin 3 Credits", 5},
                                                 {"1 Coin 4 Credits", 4},
                                                 {"1 Coins 2 Credit", {}},
                                                 {"3 Coins 1 Credit", 2},
                                                 {"4 Coins 1 Credit", 1},
                                                 {"5 Coins 1 Credit", 0}}};
constexpr std::array<DipValue, 8> mahjong_difficulty{{{"Normal", 7},
                                                      {"Easiest", 6},
                                                      {"Very Easy", 5},
                                                      {"Easy", 4},
                                                      {"Normal+", 3},
                                                      {"Hard", 2},
                                                      {"Very Hard", 1},
                                                      {"Most Hard", 0}}};
constexpr DipSetting free_play{"Free Play", "false", 0, 128, low, true};
constexpr DipSetting coins{"Coinage", "1 Coin 1 Credit", 0, 7, mahjong_coins};
constexpr DipSetting demo{"Demo Sounds", "true", 1, 32, low, true};
constexpr DipSetting continue_play{"Allow Continue", "false", 1, 16, high, true};
constexpr DipSetting kuitan{"Kuitan", "false", 1, 8, high, true};
constexpr DipSetting difficulty{"Difficulty", "Normal", 1, 7, mahjong_difficulty};
constexpr std::array<DipSetting, 7> hipai{
    {test2, free_play, coins, demo, continue_play, kuitan, difficulty}};
constexpr std::array<DipSetting, 8> hipai2{{test2,
                                            free_play,
                                            {"Backup Settings", "false", 0, 64, low, true},
                                            coins,
                                            demo,
                                            continue_play,
                                            kuitan,
                                            difficulty}};
constexpr std::array<DipValue, 8> dolls_coins{{{"1 Coin 1 Credit", 7},
                                               {"1 Coin 2 Credits", 3},
                                               {"1 Coin 3 Credits", 5},
                                               {"1 Coin 4 Credits", 1},
                                               {"2 Coins 1 Credit", 6},
                                               {"3 Coins 1 Credit", 2},
                                               {"4 Coins 1 Credit", 4},
                                               {"5 Coins 1 Credit", 0}}};
constexpr std::array<DipValue, 2> area{{{"70%", 0}, {"80%", 1}}};
constexpr std::array<DipValue, 2> controls{{{"JAMMASTICK", 0}, {"JOYSTICK", 1}}};
constexpr std::array<DipValue, 4> lives{{{"4", 3}, {"3", 2}, {"2", 1}, {"1", 0}}};
constexpr std::array<DipValue, 4> dolls_difficulty{
    {{"Normal", 3}, {"Easy", 2}, {"Hard", 1}, {"Hardest", 0}}};
constexpr std::array<DipSetting, 6> dolls{{test2,
                                           {"Coinage", "1 Coin 1 Credit", 0, 7, dolls_coins},
                                           {"Qualify Area", "70%", 1, 32, area},
                                           {"Controls", "JOYSTICK", 1, 32, controls},
                                           {"Lives", "4", 1, 12, lives},
                                           {"Difficulty", "Normal", 1, 3, dolls_difficulty}}};

} // namespace

std::span<const DipSetting> arcade_dip_settings(ArcadeProfile profile) {
  switch (profile) {
  case ArcadeProfile::StarSoldier:
    return soldier;
  case ArcadeProfile::HiPai:
    return hipai;
  case ArcadeProfile::HiPai2:
    return hipai2;
  case ArcadeProfile::VividDolls:
    return dolls;
  case ArcadeProfile::DoncDoon:
  case ArcadeProfile::KuruFever:
    return first_test;
  case ArcadeProfile::ElevenBeat:
  case ArcadeProfile::Mayjin3:
  case ArcadeProfile::TowerShaft:
  case ArcadeProfile::SuperRealMahjong:
  case ArcadeProfile::MagicalTetris:
    return second_test;
  default:
    return {};
  }
}

} // namespace cupid::n64
