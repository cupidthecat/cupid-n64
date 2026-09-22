#include "cupid/system.hpp"
#include "cupid/test_report.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace cupid;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void run_suite(System& system, TestReport& report, const char* phase) {
    for (u64 steps = 0; steps < 4000000000ULL && !report.complete && !system.cpu.frozen; ++steps)
        system.cpu.step();
    std::cout << phase << ": complete=" << report.complete << " tests=" << report.tests
              << " failed=" << report.failed << " PC=" << std::hex << system.cpu.pc << std::dec << std::endl;
    require(report.complete, "The cartridge did not complete its test report.");
    require(report.tests != 0 && !report.failed, "The cartridge reported failing or missing tests.");
    require(!system.cpu.frozen, "The CPU stalled during the cartridge run.");
    require(!system.bus.pif_boot.failed(), "PIF boot failed during the cartridge run.");
}

void test_reset(const char* cartridge, const char* firmware) {
    auto system = std::make_unique<System>();
    std::string error;
    if (!system->load_rom(cartridge, error) || !system->load_pif(firmware, error) ||
        !system->boot_cartridge(error))
        throw std::runtime_error(error);

    TestReport report;
    system->bus.debug_output = [&](std::string_view output) {
        std::cout << output << std::flush;
        report.append(output);
    };
    run_suite(*system, report, "Cold boot");
    const u64 cold_tests = report.tests;
    for (unsigned steps = 0; steps < 100000 && !system->cpu.frozen; ++steps)
        system->cpu.step();

    system->bus.write(0x31c, 4, 0x1badb002);
    system->set_reset_button(true);
    system->set_reset_button(false);
    require(system->bus.pif_boot.pre_nmi(), "Boot did not arm the reset button.");
    const u64 start = system->cpu.cycles;
    bool nmi = false;
    for (u64 steps = 0; steps < 200000000 && !system->cpu.frozen; ++steps) {
        system->cpu.step();
        if (system->cpu.pc == 0xffffffffbfc00000ULL) {
            nmi = true;
            break;
        }
    }
    require(nmi, "The reset button did not deliver NMI.");
    require(system->cpu.cycles - start >= 46875000, "NMI arrived before the pre-NMI deadline.");
    require(system->bus.read(0x31c, 4) == 0x1badb002U, "NMI cleared the reserved RAM marker.");
    require((system->bus.pif[0x7e5] & 2U) != 0, "The PIF did not expose the warm-start flag.");
    require(!system->bus.pif_boot.rom_locked(), "NMI left the boot ROM locked.");

    report = {};
    run_suite(*system, report, "Warm boot");
    require(report.tests == cold_tests, "Cold and warm boot ran different numbers of tests.");
    system->set_reset_button(true);
    require(system->bus.pif_boot.pre_nmi(), "Warm boot did not rearm the reset button.");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: cupid-warm-reset-tests CARTRIDGE PIF_ROM\n";
        return 2;
    }
    try {
        test_reset(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Warm-reset validation failed: " << error.what() << '\n';
        return 1;
    }
}
