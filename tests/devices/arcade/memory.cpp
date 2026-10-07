#include "../fixture.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;

void arcade_memory_tests() {
  for (auto profile : {ArcadeProfile::Standard, ArcadeProfile::MagicalTetris}) {
    ConsoleConfig config;
    config.expansion = false;
    config.region = VideoRegion::Pal;
    config.cic = CicModel::N7102;
    config.disk_drive = true;
    config.arcade_profile = profile;
    Console console(config);
    equal(console.ram().size(), 8 * 1024 * 1024);
    equal(console.arcade() != nullptr, true);
    std::array<std::uint8_t, 4096> cartridge{};
    cartridge[0] = 0x80;
    cartridge[1] = 0x37;
    cartridge[2] = 0x12;
    cartridge[3] = 0x40;
    std::array<std::uint8_t, 0x800> firmware{};
    firmware[0] = 0x12;
    firmware[1] = 0x34;
    firmware[2] = 0x56;
    firmware[3] = 0x78;
    equal(console.load(cartridge, firmware), true);
    equal(console.pif().read_word(0x1fc00000), 0x12345678);
    console.pif().tick();
    equal(console.pif().ram()[0x26], 0xac);
    equal(console.pif().ram()[0x27], 0xac);
    console.audio().write_word(16, 0);
    equal(console.audio().frequency(), 48681818);
    console.write(0xc0001000, 4, 0x12345678);
    equal(console.read(0xc0001000, 4).value, 0x12345678);
    equal(console.read(0xc0401000, 4).value, 0x12345678);
    equal(console.read(0x80001000, 4).value, 0x12345678);
    equal(console.read(0xc0001001, 1).value, 0x34);
    equal(console.read(0xc0001002, 2).value, 0x5678);
    console.write(0xc0001001, 1, 0xab);
    console.write(0xc0001002, 2, 0xcdef);
    equal(console.read(0xc0001000, 4).value, 0x12abcdef);
    console.write(0xc0001010, 8, 0x1234567889abcdefull);
    equal(console.read(0xc0001010, 4).value, 0);
    equal(console.read(0xc0001014, 4).value, 0x89abcdef);
    std::array<std::uint32_t, 8> words{1, 2, 3, 4, 5, 6, 7, 8};
    equal(console.write_burst(0xc0002000, words).success, true);
    std::array<std::uint32_t, 8> output{};
    equal(console.read_burst(0xc0002000, output).success, true);
    equal(output == words, true);
    equal(console.read_burst(0xc0402000, output).success, true);
    for (auto word : output)
      equal(word, 0);
    equal(console.write_burst(0xc0402000, words).success, true);
    equal(console.read(0xc0002000, 4).value, 1);
    const bool e90 = profile == ArcadeProfile::MagicalTetris;
    const auto video = e90 ? 0xd0000000u : 0xd0800000u;
    const auto palette = e90 ? 0xd0010000u : 0xd0801000u;
    const auto registers = e90 ? 0xd0030000u : 0xd0802000u;
    console.write(video, 4, 0x01234567);
    console.write(palette, 4, 0x89abcdef);
    equal(console.read(video, 4).value, 0x01234567);
    equal(console.read(palette, 4).value, 0x89abcdef);
    equal(console.read(registers, 4).value, 1);
    console.write(registers + 0x1e, 2, 1);
    equal(console.read(registers, 4).value, 0);
    equal(console.read(registers + 4, 4).value, 0xffffffff);
    console.power(true);
    equal(console.read(0xc0001000, 4).value, 0x12abcdef);
    equal(console.read(video, 4).value, 0x01234567);
    equal(console.read(palette, 4).value, 0x89abcdef);
    console.power();
    equal(console.read(0xc0001000, 4).value, 0);
    equal(console.read(video, 4).value, 0);
    equal(console.read(palette, 4).value, 0);
    equal(console.read(registers, 4).value, 0);
    equal(console.read(0xe0000000, 4).value, 0xffffffff);
    equal(console.frozen(), false);
    equal(console.read_burst(video, output).success, false);
    equal(console.frozen(), true);
    console.power();
    console.read(0xc0000000, 8);
    equal(console.frozen(), true);
  }
  Console console;
  equal(console.arcade() == nullptr, true);
  console.read(0xc0000000, 4);
  equal(console.frozen(), true);
}

} // namespace test
