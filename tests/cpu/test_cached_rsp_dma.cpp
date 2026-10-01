#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

void prepare(System& system, unsigned phase, bool native, bool interrupts) {
    test::initialize_memory(system);
    system.cpu.set_native_execution(native);
    system.cpu.write_cop0(12, interrupts ? 0x34000401U : 0x34000000U);
    constexpr std::array cpu_program{
        0x8e820000U, 0x00401821U, 0x24210001U, 0x38640055U,
        0x00812825U, 0x24a60001U, 0x08000400U, 0x24c70001U,
    };
    for (unsigned index = 0; index < cpu_program.size(); ++index)
        system.bus.write(0x1000U + index * 4U, 4, cpu_program[index]);
    for (u32 offset = 0; offset < 2048U; offset += 4U) {
        system.bus.write(0x3000U + offset, 4, 0x24210001U);
        system.bus.write(0x04000000U + offset, 4, 0x24010007U);
    }
    system.cpu.gpr[20] = 0xffffffff80003000ULL;
    system.cpu.set_pc(0xffffffff80001000ULL);
    for (unsigned index = 0; index < 128; ++index)
        system.cpu.step();
    system.advance(phase);
    system.bus.write(0x0430000cU, 4, 2U);
}

void compare(System& batched, System& stepped, unsigned budget) {
    for (unsigned index = 0; index < budget; ++index)
        stepped.cpu.step();
    CHECK_EQ(batched.cpu.run_slice(budget, 1000000U), budget);
    batched.settle();
    stepped.settle();
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK_EQ(batched.cpu.pc, stepped.cpu.pc);
    CHECK_EQ(batched.cpu.next_pc, stepped.cpu.next_pc);
    CHECK_EQ(batched.cpu.cycles, stepped.cpu.cycles);
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.bus.memory.clock(), stepped.bus.memory.clock());
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10U), stepped.rsp.read_register(0x10U));
    CHECK_EQ(batched.rsp.read_register(0x00U), stepped.rsp.read_register(0x00U));
    CHECK_EQ(batched.rsp.read_register(0x04U), stepped.rsp.read_register(0x04U));
    CHECK(batched.rsp.memory == stepped.rsp.memory);
    CHECK(batched.bus.rdram == stepped.bus.rdram);
    for (unsigned index = 0; index < batched.cpu.data_cache.size(); ++index) {
        const auto& a = batched.cpu.data_cache[index];
        const auto& b = stepped.cpu.data_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
        CHECK_EQ(a.dirty, b.dirty);
    }
}

void start_dma(System& system, bool writing, bool imem, bool running) {
    if (running) {
        constexpr std::array rsp_program{0x24210001U, 0x24420001U, 0x24630001U, 0x24840001U, 0x1000fffbU, 0U};
        for (unsigned index = 0; index < rsp_program.size(); ++index)
            system.bus.write(0x04001300U + index * 4U, 4, rsp_program[index]);
        system.rsp.write_pc(0x300U);
        system.rsp.write_register(0x10U, 0x101U);
    }
    const u32 bank = imem ? 0x1000U : 0U;
    const u32 length = 127U | (7U << 12U);
    system.rsp.write_register(0x00U, bank | 0x300U);
    system.rsp.write_register(0x04U, 0x3000U);
    system.rsp.write_register(writing ? 0x0cU : 0x08U, length);
    system.rsp.write_register(0x00U, bank | 0x700U);
    system.rsp.write_register(0x04U, 0x3400U);
    system.rsp.write_register(writing ? 0x0cU : 0x08U, length);
    CHECK_EQ(system.rsp.read_register(0x10U) & 0xcU, 0xcU);
}
} // namespace

TEST(cpu_cached_slices_preserve_queued_sp_dma_rows_and_stale_cache_data) {
    for (const bool native : {false, true}) {
        for (const bool interrupts : {false, true}) {
            for (const bool writing : {false, true}) {
                for (unsigned phase = 0; phase < 3; ++phase) {
                    auto machines = std::make_unique<std::array<System, 2>>();
                    auto& [batched, stepped] = *machines;
                    for (auto* system : {&batched, &stepped}) {
                        prepare(*system, phase, native, interrupts);
                        start_dma(*system, writing, false, false);
                    }
                    compare(batched, stepped, 15U);
                    CHECK((batched.rsp.read_register(0x10U) & 4U) != 0);
                    CHECK(batched.cpu.batched_cached_instructions() != 0);
                    for (unsigned budget : {1U, 2U, 3U, 7U, 31U, 63U, 127U, 511U})
                        compare(batched, stepped, budget);
                    CHECK_EQ(batched.rsp.read_register(0x10U) & 0xcU, 0U);
                    CHECK_EQ(batched.cpu.gpr[2], 0x24210001U);
                    CHECK_EQ(batched.bus.memory.read(0x3000U, 4), writing ? 0x24010007U : 0x24210001U);
                }
            }
        }
    }
}

TEST(cpu_cached_slices_observe_sp_dma_instruction_replacement_at_the_row_edge) {
    for (const bool native : {false, true}) {
        for (const bool interrupts : {false, true}) {
            for (unsigned phase = 0; phase < 3; ++phase) {
                auto machines = std::make_unique<std::array<System, 2>>();
                auto& [batched, stepped] = *machines;
                for (auto* system : {&batched, &stepped}) {
                    prepare(*system, phase, native, interrupts);
                    start_dma(*system, false, true, true);
                }
                compare(batched, stepped, 15U);
                CHECK(batched.cpu.batched_cached_instructions() != 0);
                for (unsigned budget : {1U, 2U, 3U, 7U, 31U, 63U, 127U, 511U})
                    compare(batched, stepped, budget);
                CHECK_EQ(batched.rsp.read_register(0x10U) & 0xcU, 0U);
            }
        }
    }
}
