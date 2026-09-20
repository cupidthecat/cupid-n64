#include "cupid/storage/file.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t image_size = 128U * 1024U;

bool number(std::string_view text, unsigned& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

int write_images(int argc, char** argv) {
    if (argc != 7)
        return 2;
    unsigned fill = 0, iterations = 0;
    if (!number(argv[3], fill) || fill > 0xff || !number(argv[6], iterations) || iterations == 0)
        return 2;

    const std::filesystem::path destination = argv[2];
    const std::filesystem::path ready = argv[4];
    const std::filesystem::path start = argv[5];
    {
        std::ofstream marker(ready, std::ios::binary | std::ios::trunc);
        if (!marker)
            return 3;
        marker << "ready";
    }

    for (unsigned wait = 0; wait < 10000 && !std::filesystem::exists(start); ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!std::filesystem::exists(start))
        return 4;

    const std::vector<cupid::u8> bytes(image_size, static_cast<cupid::u8>(fill));
    for (unsigned iteration = 0; iteration < iterations; ++iteration) {
        std::string error;
        if (!cupid::storage::replace_file(destination, bytes, error)) {
            std::cerr << error << '\n';
            return 5;
        }
    }
    return 0;
}

int verify_image(int argc, char** argv) {
    if (argc != 5)
        return 2;
    unsigned first = 0, second = 0;
    if (!number(argv[3], first) || first > 0xff || !number(argv[4], second) || second > 0xff)
        return 2;
    std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(image_size))
        return 6;
    input.seekg(0);
    std::vector<cupid::u8> bytes(image_size);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input)
        return 6;
    const auto matches = [&](unsigned value) {
        return std::all_of(bytes.begin(), bytes.end(),
                           [value](cupid::u8 byte) { return byte == static_cast<cupid::u8>(value); });
    };
    return matches(first) || matches(second) ? 0 : 7;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    const std::string_view command = argv[1];
    if (command == "write")
        return write_images(argc, argv);
    if (command == "verify")
        return verify_image(argc, argv);
    return 2;
}
