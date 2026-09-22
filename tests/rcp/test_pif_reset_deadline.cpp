#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 reset_delay = 31'250'000ULL;

void pif_command(System& system, u8 value) {
    system.bus.write(0x1fc007fc, 4, value);
    system.bus.tick(50'000);
}

void boot_pif(System& system) {
    pif_command(system, 0x10);
    for (unsigned index = 0; index < 6; ++index)
        system.bus.pif[0x7f2 + index] = static_cast<u8>(system.bus.cic.checksum() >> ((5 - index) * 8));
    pif_command(system, 0x20);
    pif_command(system, 0x40);
    pif_command(system, 8);
}

void warm_instruction_cache_line(System& system) {
    const u32 physical = static_cast<u32>(code) & 0x1fffffffU;
    auto& line = system.cpu.instruction_cache[(code >> 5) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    for (unsigned byte = 0; byte < line.data.size(); ++byte)
        line.data[byte] = system.bus.rdram[(physical & ~31U) + byte];
}

} // namespace

TEST(pif_reset_release_refreshes_the_deferred_deadline_before_plain_cpu_steps) {
    System system;
    test::initialize_memory(system);
    boot_pif(system);
    system.cpu.write_cop0(12, 0x34000000U);

    system.bus.write(0x1000, 4, 0x25080001U); // ADDIU t0,t0,1.
    system.bus.write(0x1004, 4, 0x39090055U); // XORI t1,t0,0x55.
    system.bus.write(0x1008, 4, 0x340a0066U); // ORI t2,zero,0x66; must not execute.
    system.cpu.gpr[8] = 1;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system);

    system.set_reset_button(true);
    system.bus.tick(reset_delay);
    CHECK(system.bus.pif_boot.pre_nmi());

    // Materialize the held-reset deadline. Releasing the button changes the PIF's
    // next event to one RCP cycle, so the cached deadline must be invalidated here.
    system.advance(0);
    system.set_reset_button(false);

    // No run_slice(), event query, or explicit settle may rescue a stale deadline.
    system.cpu.step();
    system.cpu.step();
    system.cpu.step();

    CHECK_EQ(system.cpu.instruction_count, 2U);
    CHECK_EQ(system.cpu.gpr[8], 2U);
    CHECK_EQ(system.cpu.gpr[9], 0x57U);
    CHECK_EQ(system.cpu.gpr[10], 0U);
    CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
    CHECK_EQ(system.cpu.cp0[30], code + 8);
}
