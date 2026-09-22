#include "cupid/system.hpp"
#include "cupid/test_report.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace cupid;

struct Observation {
    std::string text;
    u64 pc;
    u64 cycles;
    u64 instructions;
    std::array<u64, 32> registers;
    std::array<u64, 32> cop0;

    bool operator==(const Observation&) const = default;
};

u64 run(const char* cartridge, const char* firmware, bool extended, bool batched,
        std::vector<Observation>& reference) {
    auto system = std::make_unique<System>();
    std::string error;
    if (!system->load_rom(cartridge, error) || !system->load_pif(firmware, error) ||
        !system->boot_cartridge(error))
        throw std::runtime_error(error);

    TestReport report;
    if (extended)
        report.expect_extended();
    std::size_t observations = 0;
    system->bus.debug_output = [&](std::string_view text) {
        if (report.complete)
            return;
        const auto& cpu = system->cpu;
        Observation current{std::string(text), cpu.pc, cpu.cycles, cpu.instruction_count, cpu.gpr, cpu.cp0};
        if (batched) {
            if (observations >= reference.size() || current != reference[observations]) {
                std::cerr << "Observation " << observations << " differs: PC=" << std::hex << current.pc
                          << std::dec << " cycles=" << current.cycles
                          << " instructions=" << current.instructions << '\n';
                if (observations < reference.size()) {
                    const auto& expected = reference[observations];
                    std::cerr << "Expected PC=" << std::hex << expected.pc << std::dec
                              << " cycles=" << expected.cycles << " instructions=" << expected.instructions
                              << "\nExpected output: " << expected.text;
                }
                std::cerr << "Actual output: " << text;
                throw std::runtime_error("Batched execution changed a cartridge report observation.");
            }
        } else {
            reference.push_back(std::move(current));
        }
        ++observations;
        report.append(text);
    };

    constexpr u64 maximum_steps = 4000000000ULL;
    u64 steps = 0;
    while (steps < maximum_steps && !report.complete && !system->cpu.frozen) {
        if (batched) {
            const auto budget = static_cast<unsigned>(std::min<u64>(65536, maximum_steps - steps));
            const unsigned used = system->cpu.run_slice(budget, 93750);
            if (used == 0)
                throw std::runtime_error("Batched execution made no progress.");
            steps += used;
        } else {
            system->cpu.step();
            ++steps;
        }
    }
    if (!report.complete || report.tests == 0 || report.failed || system->cpu.frozen ||
        system->bus.pif_boot.failed())
        throw std::runtime_error("The cartridge did not finish all required tests successfully.");
    if (batched && observations != reference.size())
        throw std::runtime_error("Batched execution emitted a different number of report observations.");
    std::cout << (batched ? "Batched" : "Stepped") << " cartridge: tests=" << report.tests
              << " observations=" << observations
              << " idle_instructions=" << system->cpu.batched_idle_instructions() << '\n';
    return report.tests;
}

} // namespace

int main(int argc, char** argv) {
    const bool extended = argc == 4 && std::string_view(argv[3]) == "--extended";
    if (argc != 3 && !extended) {
        std::cerr << "Usage: cupid-batched-execution-tests CARTRIDGE PIF_ROM [--extended]\n";
        return 2;
    }
    try {
        std::vector<Observation> reference;
        const u64 stepped_tests = run(argv[1], argv[2], extended, false, reference);
        const u64 batched_tests = run(argv[1], argv[2], extended, true, reference);
        if (stepped_tests != batched_tests)
            throw std::runtime_error("Stepped and batched execution reported different test totals.");
        std::cout << "All cartridge output, CPU registers, and report timestamps match.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Batched cartridge validation failed: " << error.what() << '\n';
        return 1;
    }
}
