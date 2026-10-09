#include "../support/test.hpp"
#include "desktop/session.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

void save(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  if (!output)
    throw std::runtime_error("Could not write session fixture.");
}

std::vector<std::uint8_t> firmware(bool draw) {
  std::vector<std::uint32_t> code;
  const auto store = [&](std::uint32_t address, std::uint32_t value) {
    code.push_back(0x3c010000 | (address >> 16));
    code.push_back(0x34210000 | (address & 0xffff));
    code.push_back(0x3c020000 | (value >> 16));
    code.push_back(0x34420000 | (value & 0xffff));
    code.push_back(0xac220000);
  };
  store(0xa4700008, 0);
  store(0xa470000c, 0x14);
  store(0xa4300000, 0x10f);
  store(0xa3f80008, 0x00080008);
  for (unsigned n = 0; n < 4; ++n) {
    store(0xa3f0000c, 0x02000000);
    store(0xa3f00004, ((4 + n) * 2) << 26);
  }
  for (unsigned n = 0; n < 4; ++n)
    store(0xa3f00004 + ((4 + n) * 2) * 0x400, n * 2 << 26);
  store(0xa0001000, 0xf80007c0);
  store(0xa4400004, 0x1000);
  store(0xa4400008, 1);
  store(0xa4400018, 525);
  store(0xa440001c, 3092);
  store(0xa4400020, 0x0c150c15);
  store(0xa4400024, (108u << 16) | 128);
  store(0xa4400028, (34u << 16) | 36);
  store(0xa4400000, 2);
  store(0xa0002000, draw ? 0x36000000 : 0x29000000);
  store(0xa0002004, 0);
  store(0xa4100000, 0x2000);
  store(0xa4100004, 0x2008);
  code.push_back(0x1000ffff);
  code.push_back(0);
  std::vector<std::uint8_t> bytes(0x7c0);
  for (unsigned n = 0; n < code.size(); ++n)
    for (unsigned b = 0; b < 4; ++b)
      bytes.at(n * 4 + b) = static_cast<std::uint8_t>(code[n] >> (24 - b * 8));
  return bytes;
}

} // namespace

int main(int argc, char **argv) {
  const bool automatic = argc == 2 && std::string_view(argv[1]) == "--unavailable";
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path() / ("cupid-session-video-" + std::to_string(unique));
  std::filesystem::create_directory(directory);
  const auto rom = directory / "cartridge.z64";
  const auto pif = directory / "firmware.bin";
  std::vector<std::uint8_t> cartridge(4096);
  cartridge[0] = 0x80;
  cartridge[1] = 0x37;
  cartridge[2] = 0x12;
  cartridge[3] = 0x40;
  cartridge[0x3e] = 'E';
  save(rom, cartridge);
  cupid::desktop::Audio audio;
  audio.muted = true;
  for (bool draw : {false, true}) {
    save(pif, firmware(draw));
    cupid::desktop::Session session(rom, pif, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0,
                                    automatic);
    bool stopped = false;
    try {
      const auto limit = session.clocks() + cupid::n64::clock_frequency;
      while (session.frames < 2 && session.clocks() < limit)
        session.run(0, 0, 0);
    } catch (const std::runtime_error &error) {
      stopped = true;
      test::equal(std::string_view(error.what()) ==
                      "Hardware rendering is unavailable. This game requires it for RDP graphics.",
                  true);
    }
    test::equal(stopped, draw);
    if (!draw) {
      test::equal(session.frames, 2);
      session.finish_frame();
      test::equal(session.frame.width, 640);
      test::equal(session.frame.height, 480);
      if (session.frame.rgba.size() == 640 * 480 * 4) {
        const auto offset = 8 * 4;
        test::equal(session.frame.rgba[offset + 0], 255);
        test::equal(session.frame.rgba[offset + 1], 0);
        test::equal(session.frame.rgba[offset + 2], 0);
        test::equal(session.frame.rgba[offset + 3], 255);
      } else {
        test::equal(session.frame.rgba.size(), 640 * 480 * 4);
      }
    }
    session.reset();
    test::equal(session.frames, 0);
    test::equal(session.frame.rgba.size(), 0);
    if (!draw) {
      const auto limit = session.clocks() + cupid::n64::clock_frequency;
      while (session.frames < 2 && session.clocks() < limit)
        session.run(0, 0, 0);
      session.finish_frame();
      test::equal(session.frames, 2);
      test::equal(session.frame.width, 640);
      test::equal(session.frame.rgba.at(8 * 4), 255);
    }
  }
  std::filesystem::remove(rom);
  std::filesystem::remove(pif);
  std::filesystem::remove(directory);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
