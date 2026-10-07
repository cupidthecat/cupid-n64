#include "core/cartridge/profile/profile.hpp"
#include "core/cartridge/profile/boards.hpp"
#include "core/cartridge/profile/boot.hpp"
#include <string_view>

namespace cupid::n64 {
namespace {
void select_save(CartridgeProfile &profile, cartridge::SaveChip chip) {
  using cartridge::SaveChip;
  switch (chip) {
  case SaveChip::Eeprom512:
    profile.eeprom_size = 512;
    break;
  case SaveChip::Eeprom2048:
    profile.eeprom_size = 2048;
    break;
  case SaveChip::Sram32768:
    profile.sram_size = 32768;
    break;
  case SaveChip::Sram98304:
    profile.sram_size = 98304;
    break;
  case SaveChip::FlashMx29l1100:
    profile.flash_model = FlashModel::Mx29l1100;
    break;
  case SaveChip::FlashGeneric:
    profile.flash_model = FlashModel::Mx29l1101A;
    break;
  case SaveChip::None:
    break;
  }
}

CicModel pal_chip(CicModel chip) {
  switch (chip) {
  case CicModel::N6102:
    return CicModel::N7101;
  case CicModel::N6103:
    return CicModel::N7103;
  case CicModel::N6105:
    return CicModel::N7105;
  case CicModel::N6106:
    return CicModel::N7106;
  default:
    return chip;
  }
}
} // namespace

std::optional<CartridgeProfile> inspect_cartridge(std::span<const std::uint8_t> image) {
  if (image.size() < 4096 || image.size() > 0x0fc00000 || (image.size() & 3))
    return {};
  CartridgeProfile profile;
  if (const auto boot = cartridge::identify_boot(image.subspan(0x40, 0xfc0))) {
    profile.byte_swap = boot->byte_swap;
    profile.cic = boot->model;
  } else {
    const auto magic = (std::uint32_t(image[0]) << 24) | (std::uint32_t(image[1]) << 16) |
                       (std::uint32_t(image[2]) << 8) | image[3];
    switch (magic) {
    case 0x80371240:
      break;
    case 0x37804012:
      profile.byte_swap = 1;
      break;
    case 0x40123780:
      profile.byte_swap = 3;
      break;
    default:
      return {};
    }
  }
  const auto byte = [&](unsigned offset) { return image[offset ^ profile.byte_swap]; };
  const auto region = byte(0x3e);
  if (region && std::string_view("DFHILPSUWXYZ").find(char(region)) != std::string_view::npos) {
    profile.region = VideoRegion::Pal;
    profile.cic = pal_chip(profile.cic);
  }
  const std::array<char, 3> id_bytes{char(byte(0x3b)), char(byte(0x3c)), char(byte(0x3d))};
  const std::string_view id(id_bytes.data(), id_bytes.size());
  const auto revision = byte(0x3f);
  unsigned accessories = 0;
  for (const auto &board : cartridge::boards) {
    bool found = false;
    for (std::size_t offset = 0; offset < board.ids.size(); offset += 4)
      found |= board.ids.substr(offset, 3) == id;
    if (found) {
      select_save(profile, board.save);
      accessories = board.accessories;
      break;
    }
  }
  if (id == "N3H") {
    profile.sram_size = region == 'J' ? 32768 : 0;
    accessories = region == 'J' ? 2 : 3;
  } else if (id == "ND3" || id == "ND4") {
    profile.eeprom_size = region == 'J' ? 2048 : 0;
    accessories = region == 'J' ? 2 : 1;
  } else if (id == "NSM" || id == "NWR") {
    profile.eeprom_size = 512;
    accessories = id == "NWR" ? 1 : 0;
    if (region == 'J' && revision == (id == "NWR" ? 2 : 3))
      accessories |= 2;
  } else if (id == "NK4") {
    const bool sram = region == 'J' && revision < 2;
    profile.sram_size = sram ? 32768 : 0;
    profile.eeprom_size = sram ? 0 : 2048;
    accessories = 2;
  } else if (id == "NDK" && region == 'J') {
    profile.eeprom_size = 512;
  } else if (id == "NWT") {
    profile.eeprom_size = region == 'J' ? 512 : 0;
    accessories = region == 'J' ? 0 : 1;
  }
  profile.rtc_present = (accessories & 8) != 0;
  if (accessories & 4) {
    profile.accessories[0] = ControllerAccessory::Transfer;
  } else {
    if (accessories & 1)
      profile.accessories[0] = ControllerAccessory::Memory;
    if (accessories & 2)
      profile.accessories[accessories & 1 ? 1 : 0] = ControllerAccessory::Rumble;
  }
  if (id.substr(1) == "ED") {
    switch (revision >> 4) {
    case 1:
      profile.eeprom_size = 512;
      break;
    case 2:
      profile.eeprom_size = 2048;
      break;
    case 3:
      profile.sram_size = 32768;
      break;
    case 4:
      profile.sram_size = 98304;
      break;
    case 5:
      profile.flash_model = FlashModel::Mx29l1101A;
      break;
    case 6:
      profile.sram_size = 131072;
      break;
    default:
      break;
    }
    profile.rtc_present |= (revision & 1) != 0;
    bool configured = false;
    for (unsigned port = 0; port < 4; ++port) {
      const auto controller = byte(0x34 + port);
      configured |= controller != 0;
      switch (controller) {
      case 1:
        profile.accessories[port] = ControllerAccessory::Rumble;
        break;
      case 2:
        profile.accessories[port] = ControllerAccessory::Memory;
        break;
      case 3:
        profile.accessories[port] = ControllerAccessory::Transfer;
        break;
      default:
        break;
      }
    }
    if (!configured)
      profile.accessories[0] = ControllerAccessory::Rumble;
  }
  return profile;
}

} // namespace cupid::n64
