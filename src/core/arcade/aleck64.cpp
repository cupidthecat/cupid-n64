#include "core/arcade/aleck64.hpp"
#include <algorithm>

namespace cupid::n64 {

std::optional<ArcadeProfile> arcade_profile(std::string_view name) {
  if (name == "11beat")
    return ArcadeProfile::ElevenBeat;
  if (name == "starsldr")
    return ArcadeProfile::StarSoldier;
  if (name == "hipai")
    return ArcadeProfile::HiPai;
  if (name == "hipai2")
    return ArcadeProfile::HiPai2;
  if (name == "srmvs" || name == "srmvsa")
    return ArcadeProfile::SuperRealMahjong;
  if (name == "mtetrisc")
    return ArcadeProfile::MagicalTetris;
  if (name == "generic")
    return ArcadeProfile::Standard;
  if (name == "doncdoon")
    return ArcadeProfile::DoncDoon;
  if (name == "kurufev")
    return ArcadeProfile::KuruFever;
  if (name == "mayjin3")
    return ArcadeProfile::Mayjin3;
  if (name == "vivdolls")
    return ArcadeProfile::VividDolls;
  if (name == "twrshaft")
    return ArcadeProfile::TowerShaft;
  return {};
}

ArcadeProfile arcade_hardware_profile(ArcadeProfile profile) {
  switch (profile) {
  case ArcadeProfile::HiPai2:
    return ArcadeProfile::HiPai;
  case ArcadeProfile::DoncDoon:
  case ArcadeProfile::KuruFever:
  case ArcadeProfile::Mayjin3:
  case ArcadeProfile::VividDolls:
  case ArcadeProfile::TowerShaft:
    return ArcadeProfile::Standard;
  default:
    return profile;
  }
}

Aleck64::Aleck64(ArcadeProfile profile) : profile_(profile), sdram_(0x100000) {
  power();
}

void Aleck64::power(bool reset) {
  if (reset)
    return;
  std::fill(sdram_.begin(), sdram_.end(), 0);
  video_ram_.fill(0);
  palette_ram_.fill(0);
  reset_dip_switches();
  mahjong_row_ = 0;
}

} // namespace cupid::n64
