#include "cupid/host/media.hpp"

#include <algorithm>
#include <fstream>

namespace cupid::host {
namespace {

bool normalize_cartridge(std::vector<u8>& data, std::string& error) {
    const u32 order = read_be32(data.data());
    if (order == 0x37804012U) {
        for (std::size_t offset = 0; offset < data.size(); offset += 2)
            std::swap(data[offset], data[offset + 1]);
    } else if (order == 0x40123780U) {
        for (std::size_t offset = 0; offset < data.size(); offset += 4) {
            std::swap(data[offset], data[offset + 3]);
            std::swap(data[offset + 1], data[offset + 2]);
        }
    } else if (order != 0x80371240U) {
        error = "Unrecognized cartridge byte order.";
        return false;
    }
    if ((read_be32(data.data() + 8) & 3U) != 0) {
        error = "The cartridge header entry point is not aligned to a MIPS instruction.";
        return false;
    }
    return true;
}

u32 crc32(std::span<const u8> data) {
    u32 crc = 0xffffffffU;
    for (const u8 byte : data) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xedb88320U : 0U);
    }
    return ~crc;
}

} // namespace

bool read_media(const std::filesystem::path& path, MediaKind kind, std::vector<u8>& output,
                std::string& error) {
    error.clear();
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code)) {
        error = "Image is not a readable regular file: " + path_text(path);
        if (code)
            error += " (" + code.message() + ')';
        return false;
    }
    const auto size = std::filesystem::file_size(path, code);
    if (code) {
        error = "Unable to inspect image: " + path_text(path) + " (" + code.message() + ')';
        return false;
    }
    if (kind == MediaKind::Pif         ? size != 1984 && size != 2048
        : kind == MediaKind::Cartridge ? size < 0x1000 || size > 0xfc00000 || size % 4 != 0
                                       : size < 0x8000 || size > 0x800000) {
        error = kind == MediaKind::Pif ? "PIF firmware must contain 1984 or 2048 bytes."
                : kind == MediaKind::Cartridge
                    ? "A cartridge image must contain complete boot code, whole 32-bit words, and fit in the "
                      "252 MiB cartridge window."
                    : "A Game Boy cartridge image must contain between 32 KiB and 8 MiB.";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Unable to open image: " + path_text(path);
        return false;
    }
    std::vector<u8> data(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file || file.peek() != std::char_traits<char>::eof()) {
        error = "Image could not be read completely or changed while reading: " + path_text(path);
        return false;
    }
    if (kind == MediaKind::Cartridge && !normalize_cartridge(data, error))
        return false;
    output = std::move(data);
    return true;
}

std::optional<VideoStandard> header_region(std::span<const u8> cartridge) {
    if (cartridge.size() < 0x40)
        return std::nullopt;
    switch (cartridge[0x3e]) {
    case 'A':
    case 'B':
    case 'C':
    case 'E':
    case 'G':
    case 'J':
    case 'K':
    case 'N':
        return VideoStandard::Ntsc;
    case 'D':
    case 'F':
    case 'H':
    case 'I':
    case 'L':
    case 'P':
    case 'S':
    case 'U':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
        return VideoStandard::Pal;
    default:
        return std::nullopt;
    }
}

std::optional<VideoStandard> firmware_region(std::span<const u8> firmware) {
    if (firmware.size() != 1984 && firmware.size() != 2048)
        return std::nullopt;
    switch (crc32(firmware.first(1984))) {
    case 0x0aa20b09U:
        return VideoStandard::Ntsc;
    case 0xd6650859U:
        return VideoStandard::Pal;
    default:
        return std::nullopt;
    }
}

} // namespace cupid::host
