#pragma once

#include "cupid/host/options.hpp"
#include "test.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace test::host {
using namespace cupid;

class TempDirectory {
  public:
    explicit TempDirectory(std::string_view suffix = {}) {
        static std::atomic<unsigned> next = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            path_ =
                std::filesystem::temp_directory_path() / ("cupid-n64-host-" + std::to_string(stamp) + '-' +
                                                          std::to_string(++next) + std::string(suffix));
            std::error_code code;
            if (std::filesystem::create_directory(path_, code))
                return;
            if (code && code != std::errc::file_exists)
                throw std::filesystem::filesystem_error("Cannot create test directory", path_, code);
        }
        throw std::runtime_error("Cannot reserve a unique test directory.");
    }

    ~TempDirectory() {
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

    std::filesystem::path write(std::string_view name, std::span<const u8> bytes) const {
        const auto file_path = path_ / std::filesystem::path(std::u8string(name.begin(), name.end()));
        std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
        CHECK(output.good());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        CHECK(output.good());
        return file_path;
    }

    std::vector<u8> read(const std::filesystem::path& file_path) const {
        std::ifstream input(file_path, std::ios::binary);
        CHECK(input.good());
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

  private:
    std::filesystem::path path_;
};

inline std::vector<u8> n64_rom(u8 region = 'E') {
    std::vector<u8> rom(0x1000);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    rom[8] = 0x80;
    rom[10] = 0x04;
    rom[0x3e] = region;
    return rom;
}

inline std::vector<u8> pif() {
    return std::vector<u8>(1984);
}

inline std::vector<u8> game_boy_rom(u8 cartridge_type = 0x03, u8 rom_size = 0, u8 ram_size = 3) {
    std::vector<u8> rom(std::size_t{0x8000} << rom_size);
    rom[0x147] = cartridge_type;
    rom[0x148] = rom_size;
    rom[0x149] = ram_size;
    return rom;
}

inline std::optional<cupid::host::Options> parse(std::initializer_list<std::string_view> arguments,
                                                 std::string& error) {
    const std::vector<std::string_view> values(arguments);
    return cupid::host::parse_options(values, error);
}

inline cupid::host::Options base_options(const TempDirectory& directory, u8 region = 'E') {
    cupid::host::Options options;
    options.cartridge = directory.write("cartridge.z64", n64_rom(region));
    options.pif = directory.write("pif.rom", pif());
    options.pif_region = region == 'P' ? VideoStandard::Pal : VideoStandard::Ntsc;
    options.cic = CicModel::Nus6102;
    options.save = SaveType::None;
    return options;
}

} // namespace test::host
