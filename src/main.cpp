#include "cupid/host/hardware.hpp"
#include "cupid/host/options.hpp"
#include "cupid/host/storage.hpp"
#include "cupid/test_report.hpp"

#include "tasks/parallel_ranges.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

void usage() {
    std::cout << cupid::host::usage();
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

int run(std::span<const std::string_view> arguments) {
    const cupid::tasks::ParallelRangesScope render_workers;
    std::optional<cupid::host::Options> config;
    std::unique_ptr<cupid::System> system;
    try {
        std::string error;
        config = cupid::host::parse_options(arguments, error);
        if (!config) {
            if (!error.empty())
                std::cerr << error << '\n';
            usage();
            return 2;
        }
        if (config->help) {
            usage();
            return 0;
        }
        system = cupid::host::create_system(*config, error);
        if (!system) {
            std::cerr << error << '\n';
            return 2;
        }
        const auto finish = [&](int result) {
            std::string storage_error;
            if (!cupid::host::flush_persistent_storage(*system, *config, storage_error)) {
                std::cerr << storage_error << '\n';
                return 1;
            }
            return result;
        };
        std::cerr << "Hardware: " << cupid::host::describe_hardware(*system) << '\n';
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
                return finish(1);
            }
            std::cerr << "The test ROM completed " << report.tests << " tests with zero failures.\n";
            return finish(0);
        }
        dump_state(*system, history, steps);
        if (system->cpu.frozen) {
            std::cerr << "The CPU bus is stalled by an unsupported bus transaction.\n";
            return finish(1);
        }
        if (config->require_success) {
            std::cerr << "The instruction limit was reached without a complete test result.\n";
            return finish(1);
        }
        return finish(0);
    } catch (const std::exception& error) {
        if (system && config) {
            std::string storage_error;
            if (!cupid::host::flush_persistent_storage(*system, *config, storage_error))
                std::cerr << storage_error << '\n';
        }
        std::cerr << "Execution failed: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
std::string utf8_argument(std::wstring_view argument) {
    if (argument.empty())
        return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(),
                                             static_cast<int>(argument.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        throw std::runtime_error("Windows command-line argument is not valid UTF-16.");
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(), static_cast<int>(argument.size()),
                            result.data(), required, nullptr, nullptr) != required)
        throw std::runtime_error("Windows command-line argument could not be converted to UTF-8.");
    return result;
}
#endif

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> encoded;
        encoded.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index)
            encoded.push_back(utf8_argument(argv[index]));
        std::vector<std::string_view> arguments;
        arguments.reserve(encoded.size());
        for (const auto& argument : encoded)
            arguments.emplace_back(argument);
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "Argument processing failed: " << error.what() << '\n';
        return 2;
    }
}
#else
int main(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index)
        arguments.emplace_back(argv[index]);
    return run(arguments);
}
#endif
