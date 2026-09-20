#include "cupid/host/hardware.hpp"
#include "cupid/host/storage.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 hash_seed = 14695981039346656037ULL;

void hash_byte(u64& hash, u8 byte) {
    hash = (hash ^ byte) * 1099511628211ULL;
}

void hash_word(u64& hash, u32 value, unsigned bytes) {
    for (unsigned byte = bytes; byte != 0; --byte)
        hash_byte(hash, static_cast<u8>(value >> ((byte - 1U) * 8U)));
}

u64 hash_bytes(std::span<const u8> bytes) {
    u64 hash = hash_seed;
    for (u8 byte : bytes)
        hash_byte(hash, byte);
    return hash;
}

std::ofstream output_file(const std::filesystem::path& path) {
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(path, std::ios::binary);
    return file;
}

int run(std::span<const std::string_view> arguments) {
    if (arguments.size() < 3) {
        std::cerr << "Usage: cupid-compatibility-capture NEW_OUTPUT_DIRECTORY FIELD_LIMIT CARTRIDGE "
                     "--pif PIF_ROM [runner hardware options]\n";
        return 2;
    }
    unsigned limit = 0;
    const auto field_text = arguments[1];
    const auto [end, code] = std::from_chars(field_text.data(), field_text.data() + field_text.size(), limit);
    if (code != std::errc{} || end != field_text.data() + field_text.size() || limit == 0)
        throw std::runtime_error("FIELD_LIMIT must be a positive integer.");
    std::string error;
    auto options = host::parse_options(arguments.subspan(2), error);
    if (!options)
        throw std::runtime_error(error);
    if (options->help || options->require_success || options->require_extended)
        throw std::runtime_error(
            "Capture ends at the requested field count; test-report options are not supported.");
    const auto directory = host::argument_path(arguments[0]);
    if (!std::filesystem::create_directory(directory))
        throw std::runtime_error("Capture requires a new output directory.");
    auto system = host::create_system(*options, error);
    if (!system)
        throw std::runtime_error(error);
    try {
        auto metadata = output_file(directory / "run.txt");
        metadata << "hardware=" << host::describe_hardware(*system) << '\n'
                 << "field_limit=" << limit << "\ninstruction_limit=" << options->max_instructions << '\n'
                 << "normalized_rom_bytes=" << system->bus.rom.size()
                 << "\nnormalized_rom_fnv1a64=" << std::hex << hash_bytes(system->bus.rom)
                 << "\npif_code_fnv1a64=" << hash_bytes(std::span<const u8>(system->bus.pif).first(1984))
                 << std::dec << '\n';
        auto frames = output_file(directory / "fields.csv");
        frames << "field,width,height,parity,interlaced,cpu_cycles,instructions,pc,dp_status,"
                  "audio_samples,nonzero_samples,audio_fnv1a64,rgba_fnv1a64\n";
        auto audio = output_file(directory / "audio.s16be");
        unsigned fields = 0;
        u64 samples = 0, nonzero = 0, audio_hash = hash_seed;
        VideoField last_field;
        system->bus.audio_output = [&](s16 left, s16 right) {
            const u16 l = static_cast<u16>(left), r = static_cast<u16>(right);
            const char bytes[]{static_cast<char>(l >> 8U), static_cast<char>(l), static_cast<char>(r >> 8U),
                               static_cast<char>(r)};
            audio.write(bytes, 4);
            hash_word(audio_hash, l, 2);
            hash_word(audio_hash, r, 2);
            ++samples;
            nonzero += left != 0 || right != 0;
        };
        system->bus.set_video_output([&](VideoField field) {
            u64 hash = hash_seed;
            for (u32 pixel : field.pixels)
                hash_word(hash, pixel, 4);
            frames << ++fields << ',' << field.width << ',' << field.height << ',' << field.field << ','
                   << field.interlaced << ',' << system->cpu.cycles << ',' << system->cpu.instruction_count
                   << ',' << std::hex << system->cpu.pc << ',' << system->bus.rdp.read_register(12)
                   << std::dec << ',' << samples << ',' << nonzero << ',' << std::hex << audio_hash << ','
                   << hash << std::dec << '\n';
            last_field = std::move(field);
        });
        u64 steps = 0;
        while (fields < limit && steps < options->max_instructions && !system->cpu.frozen &&
               !system->bus.pif_boot.failed()) {
            system->cpu.step();
            ++steps;
        }
        if (!last_field.pixels.empty()) {
            auto image = output_file(directory / "last-field.ppm");
            image << "P6\n" << last_field.width << ' ' << last_field.height << "\n255\n";
            for (u32 pixel : last_field.pixels) {
                const char rgb[]{static_cast<char>(pixel >> 24U), static_cast<char>(pixel >> 16U),
                                 static_cast<char>(pixel >> 8U)};
                image.write(rgb, 3);
            }
            image.close();
        }
        const bool complete = fields >= limit && !system->cpu.frozen && !system->bus.pif_boot.failed();
        metadata << "fields=" << fields << "\nsteps=" << steps << "\ncpu_cycles=" << system->cpu.cycles
                 << "\ncpu_frozen=" << system->cpu.frozen << "\npif_failed=" << system->bus.pif_boot.failed()
                 << "\ndp_status=" << std::hex << system->bus.rdp.read_register(12) << std::dec
                 << "\naudio_samples=" << samples << "\ncapture_complete=" << complete << '\n';
        frames.close();
        audio.close();
        metadata.close();
        if (!host::flush_persistent_storage(*system, *options, error))
            throw std::runtime_error(error);
        std::cout << "Captured " << fields << " fields and " << samples << " audio samples in " << steps
                  << " CPU steps. This records output; it does not certify compatibility.\n";
        return complete ? 0 : 1;
    } catch (...) {
        std::string storage_error;
        if (!host::flush_persistent_storage(*system, *options, storage_error))
            std::cerr << storage_error << '\n';
        throw;
    }
}

int guarded_run(const std::vector<std::string>& arguments) {
    try {
        std::vector<std::string_view> views(arguments.begin(), arguments.end());
        return run(views);
    } catch (const std::exception& error) {
        std::cerr << "Capture failed: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index)
            arguments.push_back(cupid::host::path_text(std::filesystem::path(argv[index])));
        return guarded_run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "Capture arguments failed: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    return guarded_run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
