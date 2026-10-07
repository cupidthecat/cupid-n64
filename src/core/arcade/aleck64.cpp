#include "core/arcade/aleck64.hpp"
#include <algorithm>

namespace cupid::n64 {

std::optional<ArcadeProfile> arcade_profile(std::string_view name) {
  if (name == "11beat")
    return ArcadeProfile::ElevenBeat;
  if (name == "starsldr")
    return ArcadeProfile::StarSoldier;
  if (name == "hipai" || name == "hipai2")
    return ArcadeProfile::HiPai;
  if (name == "srmvs" || name == "srmvsa")
    return ArcadeProfile::SuperRealMahjong;
  if (name == "mtetrisc")
    return ArcadeProfile::MagicalTetris;
  for (auto standard : {"generic", "doncdoon", "kurufev", "mayjin3", "vivdolls", "twrshaft"})
    if (name == standard)
      return ArcadeProfile::Standard;
  return {};
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
  dip_switches_ = {255, 255};
  if (profile_ == ArcadeProfile::StarSoldier)
    dip_switches_ = {127, 207};
  if (profile_ == ArcadeProfile::HiPai)
    dip_switches_[1] = 199;
  mahjong_row_ = 0;
}

} // namespace cupid::n64
