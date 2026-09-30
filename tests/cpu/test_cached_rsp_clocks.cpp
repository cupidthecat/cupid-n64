#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

void prepare_clock_program(System& system, unsigned phase, bool native) {
    test::initialize_memory(system);
    system.cpu.set_native_execution(native);
    system.cpu.write_cop0(12, 0x34000401U);
    constexpr std::array cpu_program{
        0x24210001U, 0x24220001U, 0x24430001U, 0x24640001U,
        0x24850001U, 0x24a60001U, 0x08000400U, 0x24c70001U,
    };
    for (unsigned index = 0; index < cpu_program.size(); ++index)
        system.bus.write(0x1000U + index * 4U, 4, cpu_program[index]);
    system.cpu.set_pc(0xffffffff80001000ULL);
    for (unsigned index = 0; index < 128; ++index)
        system.cpu.step();
    system.advance(phase);

    u32 address = 0x04001000U;
    for (unsigned group = 0; group < 2; ++group) {
        for (unsigned index = 0; index < 64; ++index) {
            system.bus.write(address, 4, 0x24210001U); // ADDIU at,at,1.
            address += 4U;
        }
        system.bus.write(address, 4, 0x40026000U);                   // MFC0 v0,DPC_CLOCK.
        system.bus.write(address + 4U, 4, 0xac020800U + group * 4U); // SW v0,clock sample.
        address += 8U;
    }
    system.bus.write(address, 4, 0x0000000dU); // BREAK after the second sample.
    system.bus.write(0x0430000cU, 4, 2U);
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10U, 1U);
}
} // namespace

TEST(cpu_cached_rsp_local_clocks_reach_shared_reads_and_early_slice_returns) {
    for (const bool native : {false, true}) {
        for (unsigned phase = 0; phase < 3; ++phase) {
            auto machines = std::make_unique<std::array<System, 2>>();
            auto& [batched, stepped] = *machines;
            for (auto* system : {&batched, &stepped})
                prepare_clock_program(*system, phase, native);
            for (unsigned budget : {1U, 2U, 3U, 5U, 17U, 31U, 64U, 127U, 511U}) {
                for (unsigned index = 0; index < budget; ++index)
                    stepped.cpu.step();
                CHECK_EQ(batched.cpu.run_slice(budget, 1'000'000U), budget);
                // Observe clocks directly before any explicit settlement or register access.
                CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
                CHECK_EQ(batched.bus.rdp.read_register(0x10U), stepped.bus.rdp.read_register(0x10U));
                CHECK_EQ(batched.cpu.cycles, stepped.cpu.cycles);
                CHECK_EQ(batched.cpu.pc, stepped.cpu.pc);
                CHECK(batched.cpu.gpr == stepped.cpu.gpr);
                CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
                CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
                CHECK(batched.rsp.memory == stepped.rsp.memory);
            }
            const u64 first = batched.bus.read(0x04000800U, 4);
            const u64 second = batched.bus.read(0x04000804U, 4);
            CHECK(first != 0);
            CHECK(second > first);
            CHECK(batched.cpu.batched_cached_instructions() != 0);
            CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
        }
    }
}
