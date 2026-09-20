#include "host_fixture.hpp"

#include "cupid/host/media.hpp"

#include <algorithm>

using namespace cupid;
using namespace cupid::host;

TEST(host_media_normalizes_all_supported_n64_byte_orders) {
    test::host::TempDirectory directory;
    const auto canonical = test::host::n64_rom();
    auto byte_swapped = canonical;
    for (std::size_t offset = 0; offset < byte_swapped.size(); offset += 2)
        std::swap(byte_swapped[offset], byte_swapped[offset + 1]);
    auto word_swapped = canonical;
    for (std::size_t offset = 0; offset < word_swapped.size(); offset += 4) {
        std::swap(word_swapped[offset], word_swapped[offset + 3]);
        std::swap(word_swapped[offset + 1], word_swapped[offset + 2]);
    }

    std::string error;
    const std::array<std::pair<std::string_view, const std::vector<u8>*>, 3> cases{
        {{"native.z64", &canonical}, {"bytes.v64", &byte_swapped}, {"words.n64", &word_swapped}}};
    for (const auto& [name, data] : cases) {
        std::vector<u8> loaded;
        CHECK(read_media(directory.write(name, *data), MediaKind::Cartridge, loaded, error));
        CHECK_EQ(loaded, canonical);
    }
}

TEST(host_media_rejects_truncated_unaligned_and_unknown_n64_images) {
    test::host::TempDirectory directory;
    std::string error;
    std::vector<u8> loaded;
    CHECK(!read_media(directory.write("short.z64", std::vector<u8>(0xfff)), MediaKind::Cartridge, loaded,
                      error));
    CHECK(error.find("boot code") != std::string::npos);
    CHECK(!read_media(directory.write("unaligned.z64", std::vector<u8>(0x1001)), MediaKind::Cartridge, loaded,
                      error));
    CHECK(error.find("whole 32-bit words") != std::string::npos);
    CHECK(!read_media(directory.write("unknown.z64", std::vector<u8>(0x1000)), MediaKind::Cartridge, loaded,
                      error));
    CHECK(error.find("byte order") != std::string::npos);
}

TEST(host_media_rejects_unaligned_entry_points) {
    test::host::TempDirectory directory;
    auto rom = test::host::n64_rom();
    rom[11] = 2;
    std::string error;
    std::vector<u8> loaded;
    CHECK(!read_media(directory.write("unaligned-entry.z64", rom), MediaKind::Cartridge, loaded, error));
    CHECK(error.find("entry point") != std::string::npos);
}

TEST(host_media_reads_unicode_paths_without_loss) {
    test::host::TempDirectory directory;
    const auto unicode = directory.path() / std::filesystem::path(u8"測試-é.z64");
    const auto rom = test::host::n64_rom();
    {
        std::ofstream output(unicode, std::ios::binary | std::ios::trunc);
        CHECK(output.good());
        output.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    }
    std::string error;
    std::vector<u8> loaded;
    CHECK(read_media(unicode, MediaKind::Cartridge, loaded, error));
    CHECK_EQ(loaded, rom);
}

TEST(host_media_maps_supported_n64_region_codes) {
    auto rom = test::host::n64_rom();
    for (const u8 code : std::array<u8, 8>{'A', 'B', 'C', 'E', 'G', 'J', 'K', 'N'}) {
        rom[0x3e] = code;
        CHECK_EQ(header_region(rom), VideoStandard::Ntsc);
    }
    for (const u8 code : std::array<u8, 12>{'D', 'F', 'H', 'I', 'L', 'P', 'S', 'U', 'W', 'X', 'Y', 'Z'}) {
        rom[0x3e] = code;
        CHECK_EQ(header_region(rom), VideoStandard::Pal);
    }
    rom[0x3e] = '?';
    CHECK(!header_region(rom));
}
