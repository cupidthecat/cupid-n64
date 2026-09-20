#include "cupid/system.hpp"
#include "cupid/test_report.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path cartridge;
    std::filesystem::path pif;
    cupid::u64 max_instructions{4000000000ULL};
    bool require_success{};
    bool require_extended{};
};

bool number(std::string_view text, cupid::u64& result) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    return error == std::errc{} && end == text.data() + text.size();
}

void usage() {
    std::cout
        << "Usage: cupid-n64 CARTRIDGE --pif BOOT_ROM [--max-instructions COUNT] [--require-test-success] "
           "[--require-extended-tests]\n";
}

std::optional<Options> options(int argc, char** argv) {
    Options parsed;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--pif" && index + 1 < argc)
            parsed.pif = argv[++index];
        else if (arg == "--max-instructions" && index + 1 < argc) {
            if (!number(argv[++index], parsed.max_instructions) || parsed.max_instructions == 0) {
                std::cerr << "The instruction limit must be a positive integer.\n";
                return std::nullopt;
            }
        } else if (arg == "--require-extended-tests") {
            parsed.require_extended = parsed.require_success = true;
        } else if (arg == "--require-test-success")
            parsed.require_success = true;
        else if (!arg.empty() && arg.front() != '-' && parsed.cartridge.empty())
            parsed.cartridge = argv[index];
        else {
            std::cerr << "Unrecognized or incomplete argument: " << arg << '\n';
            return std::nullopt;
        }
    }
    if (parsed.cartridge.empty() || parsed.pif.empty())
        return std::nullopt;
    return parsed;
}

void dump_state(const cupid::System& system, const std::array<cupid::u64, 32>& history, cupid::u64 steps) {
    const auto& cpu = system.cpu;
    std::cerr << std::hex << std::setfill('0') << "PC=" << std::setw(16) << cpu.pc
              << " next=" << std::setw(16) << cpu.next_pc << " status=" << std::setw(8) << cpu.status()
              << " cause=" << std::setw(8) << cpu.cp0[13] << " EPC=" << std::setw(16) << cpu.cp0[14]
              << " BadVAddr=" << std::setw(16) << cpu.cp0[8] << '\n';
    for (unsigned reg = 0; reg < 32; ++reg) {
        std::cerr << "r" << std::dec << std::setw(2) << reg << '=' << std::hex << std::setw(16)
                  << cpu.gpr[reg] << (reg % 4 == 3 ? '\n' : ' ');
    }
    std::cerr << "Recent instruction addresses:";
    const cupid::u64 count = std::min<cupid::u64>(steps, history.size());
    for (cupid::u64 index = 0; index < count; ++index) {
        std::cerr << (index % 4 == 0 ? '\n' : ' ') << std::setw(16)
                  << history[(steps - count + index) % history.size()];
    }
    std::cerr << std::dec << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage();
        return 0;
    }
    const auto config = options(argc, argv);
    if (!config) {
        usage();
        return 2;
    }
    try {
        auto system = std::make_unique<cupid::System>();
        std::string error;
        if (!system->load_rom(config->cartridge, error) || !system->load_pif(config->pif, error) ||
            !system->boot_cartridge(error)) {
            std::cerr << error << '\n';
            return 2;
        }
        cupid::TestReport report;
        if (config->require_extended)
            report.expect_extended();
        system->bus.debug_output = [&](std::string_view output) {
            std::cout << output << std::flush;
            report.append(output);
        };
        std::array<cupid::u64, 32> history{};
        const auto started = std::chrono::steady_clock::now();
        cupid::u64 steps = 0;
        while (steps < config->max_instructions && !report.complete && !system->cpu.frozen) {
            history[steps % history.size()] = system->cpu.pc;
            system->cpu.step();
            ++steps;
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
        std::cerr << "Executed " << system->cpu.instruction_count << " instructions in " << elapsed.count()
                  << " seconds; CPU cycles: " << system->cpu.cycles << ".\n";
        if (report.complete) {
            if (report.tests == 0 || report.failed) {
                std::cerr << "The test ROM reported failure.\n";
                return 1;
            }
            std::cerr << "The test ROM completed " << report.tests << " tests with zero failures.\n";
            return 0;
        }
        dump_state(*system, history, steps);
        if (system->cpu.frozen) {
            std::cerr << "The CPU bus is stalled by an unsupported bus transaction.\n";
            return 1;
        }
        if (config->require_success) {
            std::cerr << "The instruction limit was reached without a complete test result.\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Execution failed: " << error.what() << '\n';
        return 1;
    }
}
