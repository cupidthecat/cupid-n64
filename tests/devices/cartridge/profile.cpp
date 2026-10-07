#include "core/cartridge/profile/profile.hpp"
#include "../fixture.hpp"
#include "core/cartridge/profile/boot.hpp"
#include "core/cartridge/rom.hpp"
#include <algorithm>
#include <string_view>

namespace test {
using namespace cupid::n64;

void cartridge_profile_tests() {
  std::vector<std::uint8_t> image(4096);
  image[0] = 0x80;
  image[1] = 0x37;
  image[2] = 0x12;
  image[3] = 0x40;
  const auto inspect = [&](std::string_view id, char region = 'E', unsigned revision = 0) {
    std::copy(id.begin(), id.end(), image.begin() + 0x3b);
    image[0x3e] = static_cast<std::uint8_t>(region);
    image[0x3f] = static_cast<std::uint8_t>(revision);
    auto profile = inspect_cartridge(image);
    equal(profile.has_value(), true);
    return profile.value_or(CartridgeProfile{});
  };
  for (char region : std::string_view("ABCEGJKN")) {
    const auto profile = inspect("NSM", region);
    equal(profile.region == VideoRegion::Ntsc, true);
    equal(profile.cic == CicModel::N6102, true);
    equal(profile.eeprom_size, 512);
    equal(profile.sram_size, 0);
    equal(profile.flash_model.has_value(), false);
  }
  for (char region : std::string_view("DFHILPSUWXYZ")) {
    const auto profile = inspect("NSM", region);
    equal(profile.region == VideoRegion::Pal, true);
    equal(profile.cic == CicModel::N7101, true);
  }
  equal(inspect("NSM", 0).region == VideoRegion::Ntsc, true);
  equal(inspect("NSM", 'J', 3).accessories[0] == ControllerAccessory::Rumble, true);
  equal(inspect("NSM", 'J', 2).accessories[0] == ControllerAccessory::None, true);
  auto profile = inspect("NS2", 'J');
  equal(profile.eeprom_size, 0);
  equal(profile.accessories[0] == ControllerAccessory::Memory, true);
  profile = inspect("NAF", 'J');
  equal(profile.flash_model == FlashModel::Mx29l1100, true);
  equal(profile.rtc_present, true);
  equal(profile.accessories[0] == ControllerAccessory::Memory, true);
  equal(inspect("NMQ").flash_model == FlashModel::Mx29l1101A, true);
  equal(inspect("NZS").flash_model == FlashModel::Mx29l1100, true);
  equal(inspect("CDZ", 'J').sram_size, 98304);
  equal(inspect("CZL", 'J').sram_size, 32768);
  equal(inspect("NPD").eeprom_size, 2048);
  equal(inspect("NPD").accessories[0] == ControllerAccessory::Transfer, true);
  profile = inspect("NT3");
  equal(profile.sram_size, 32768);
  equal(profile.accessories[0] == ControllerAccessory::Memory, true);
  equal(profile.accessories[1] == ControllerAccessory::Rumble, true);
  for (const auto id : {"ND3", "ND4"}) {
    profile = inspect(id, 'J');
    equal(profile.eeprom_size, 2048);
    equal(profile.accessories[0] == ControllerAccessory::Rumble, true);
    profile = inspect(id, 'E');
    equal(profile.eeprom_size, 0);
    equal(profile.accessories[0] == ControllerAccessory::Memory, true);
  }
  equal(inspect("N3H", 'J').sram_size, 32768);
  equal(inspect("N3H", 'E').sram_size, 0);
  equal(inspect("N3H", 'E').accessories[0] == ControllerAccessory::Memory, true);
  for (unsigned revision = 0; revision < 4; ++revision) {
    profile = inspect("NK4", 'J', revision);
    equal(profile.sram_size, revision < 2 ? 32768 : 0);
    equal(profile.eeprom_size, revision < 2 ? 0 : 2048);
    equal(inspect("NK4", 'E', revision).eeprom_size, 2048);
  }
  equal(inspect("NWR", 'J', 2).accessories[1] == ControllerAccessory::Rumble, true);
  equal(inspect("NWR", 'J', 1).accessories[1] == ControllerAccessory::None, true);
  equal(inspect("NDK", 'J').eeprom_size, 512);
  equal(inspect("NDK", 'E').eeprom_size, 0);
  equal(inspect("NWT", 'J').eeprom_size, 512);
  equal(inspect("NWT", 'E').accessories[0] == ControllerAccessory::Memory, true);
  equal(inspect("XYZ").eeprom_size, 0);
  equal(inspect("2 N").eeprom_size, 0);
  for (unsigned chip = 0; chip < 8; ++chip) {
    profile = inspect("NED", 'E', (chip << 4) | 1);
    equal(profile.eeprom_size, chip == 1 ? 512 : chip == 2 ? 2048 : 0);
    equal(profile.sram_size, chip == 3 ? 32768 : chip == 4 ? 98304 : chip == 6 ? 131072 : 0);
    equal(profile.flash_model.has_value(), chip == 5);
    equal(profile.rtc_present, true);
    equal(profile.accessories[0] == ControllerAccessory::Rumble, true);
  }
  image[0x34] = 2;
  image[0x35] = 1;
  image[0x36] = 3;
  image[0x37] = 255;
  profile = inspect("NED", 'P', 0x22);
  equal(profile.accessories[0] == ControllerAccessory::Memory, true);
  equal(profile.accessories[1] == ControllerAccessory::Rumble, true);
  equal(profile.accessories[2] == ControllerAccessory::Transfer, true);
  equal(profile.accessories[3] == ControllerAccessory::None, true);
  equal(profile.region == VideoRegion::Pal, true);
  equal(profile.rtc_present, false);
  for (unsigned swap : {0u, 1u, 3u}) {
    auto reversed = image;
    for (unsigned offset = 0; offset < image.size(); ++offset)
      reversed[offset] = image[offset ^ swap];
    auto detected = inspect_cartridge(reversed);
    equal(detected.has_value(), true);
    if (detected) {
      equal(detected->byte_swap, swap);
      equal(detected->eeprom_size, 2048);
      equal(detected->region == VideoRegion::Pal, true);
      CartridgeRom rom;
      equal(rom.load(reversed), true);
      equal(std::equal(image.begin(), image.end(), rom.data().begin()), true);
    }
  }
  equal(inspect_cartridge(std::span(image).first(4095)).has_value(), false);
  equal(inspect_cartridge({}).has_value(), false);
  image[0] = 0;
  equal(inspect_cartridge(image).has_value(), false);
  equal(cartridge::boot_checksum({}, 0x3f), 0);
  equal(cartridge::boot_checksum(image, 0x3f, 2), 0);
  constexpr unsigned seeds[]{0, 0x3f, 0x78, 0x91, 0x85, 0xac, 0xdd, 255};
  constexpr std::uint64_t checksums[3][8] = {
      {0xc51a0c0bfd64, 0x2982ad8c201d, 0xb30f78d0cad9, 0x31085bbe1381, 0xbb2af8db9c3a,
       0x3b492e705c47, 0x81aec4523e0f, 0x5a58fd2a5a06},
      {0x3b7640ce3514, 0x0f74e8302797, 0xa1adbe97298a, 0xd639f0bda20d, 0xe0fffb7bcc91,
       0xecb4fefc16f4, 0xbbc002434db3, 0xf17052a493ae},
      {0x301d2b6f6e08, 0x4962269518c5, 0x33dda7bd80b6, 0x032f5f38cfdf, 0xfec9e83e9994,
       0x2884ba11b137, 0x5bdc2988fe3f, 0xf710137a2c49},
  };
  std::array<std::uint8_t, 0xfc0> boot{};
  for (unsigned pattern = 0; pattern < 3; ++pattern) {
    for (unsigned offset = 0; offset < boot.size(); ++offset)
      boot[offset] = static_cast<std::uint8_t>(pattern == 0 ? 0 : pattern == 1 ? 255 : offset);
    for (unsigned swap : {0u, 1u, 3u}) {
      auto reversed = boot;
      for (unsigned offset = 0; offset < boot.size(); ++offset)
        reversed[offset] = boot[offset ^ swap];
      for (unsigned seed = 0; seed < 8; ++seed)
        equal(cartridge::boot_checksum(reversed, seeds[seed], swap), checksums[pattern][seed]);
    }
  }
}

} // namespace test
