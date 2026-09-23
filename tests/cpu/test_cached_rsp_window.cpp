#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 data = 0xffffffff80002000ULL;

constexpr u32 special(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned function) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | (sa << 6U) | function;
}

constexpr u32 immediate(unsigned op, unsigned rs, unsigned rt, int value) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

void write_cpu_program(System& system, std::initializer_list<u32> words) {
    const u32 base = static_cast<u32>(code) & 0x1fffffffU;
    unsigned offset = 0;
    for (const u32 word : words) {
        system.bus.write(base + offset, 4, word);
        offset += 4U;
    }
}

void warm_instruction_cache_line(System& system, u64 address) {
    const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
    const u32 base = physical & ~31U;
    auto& line = system.cpu.instruction_cache[(address >> 5U) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    for (unsigned byte = 0; byte < line.data.size(); ++byte)
        line.data[byte] = system.bus.rdram[base + byte];
}

void warm_data_cache(System& system) {
    const u32 physical = static_cast<u32>(data) & 0x1fffffffU;
    auto& line = system.cpu.data_cache[(data >> 4U) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    write_be32(line.data.data(), 0x11223344U);
}

void prepare_cpu_with_load_hazard(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    write_cpu_program(system,
                      {
                          0U,                            // Prologue that latches the cached loop.
                          immediate(0x23, 16, 8, 0),     // LW t0,0(s0).
                          special(8, 0, 9, 0, 0x21),     // ADDU t1,t0,zero: one load-use wait.
                          immediate(0x09, 10, 10, 1),    // ADDIU t2,t2,1.
                          immediate(0x0e, 10, 11, 0x55), // XORI t3,t2,0x55.
                          immediate(0x0d, 12, 12, 1),    // ORI t4,t4,1.
                          immediate(0x05, 10, 0, -6),    // BNE t2,zero,code+4.
                          immediate(0x0d, 13, 13, 1),    // ORI t5,t5,1 (delay slot).
                      });
    system.cpu.gpr[16] = data;
    system.cpu.gpr[10] = 1U;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system, code);
    warm_data_cache(system);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
}

void rsp_word(System& system, u32 address, u32 word) {
    write_be32(system.rsp.memory.data() + 0x1000U + (address & 0x0ffcU), word);
}

void start_rsp(System& system, u32 address = 0) {
    system.rsp.write_pc(address);
    system.rsp.write_register(0x10, 1U); // Clear halt.
}

unsigned stepped_slice(System& system, unsigned maximum_steps, u64 maximum_cycles = 1'000'000U) {
    const u64 start = system.cpu.cycles;
    unsigned steps = 0;
    while (steps < maximum_steps && system.cpu.cycles - start < maximum_cycles && !system.cpu.frozen) {
        system.cpu.step();
        ++steps;
    }
    return steps;
}

void equivalent(System& batched, System& stepped) {
    batched.settle();
    stepped.settle();
    CHECK_EQ(batched.cpu.cycles, stepped.cpu.cycles);
    CHECK_EQ(batched.cpu.instruction_count, stepped.cpu.instruction_count);
    CHECK_EQ(batched.cpu.pc, stepped.cpu.pc);
    CHECK_EQ(batched.cpu.next_pc, stepped.cpu.next_pc);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10), stepped.rsp.read_register(0x10));
    CHECK(batched.rsp.memory == stepped.rsp.memory);
}

void compare_slice(System& batched, System& stepped, unsigned maximum_steps,
                   u64 maximum_cycles = 1'000'000U) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    equivalent(batched, stepped);
}

void prepare_local_branch_program(System& system) {
    rsp_word(system, 0x00U, 0xc8012000U); // LQV v1,0(zero): vector load latency.
    rsp_word(system, 0x04U, 0x4a010850U); // VADD v1,v1,v1.
    rsp_word(system, 0x08U, 0x24020055U); // ADDIU v0,zero,0x55; can latch behind the stall.
    rsp_word(system, 0x0cU, 0xac020080U); // SW v0,0x80(zero).
    rsp_word(system, 0x10U, 0x24210001U); // ADDIU at,at,1.
    rsp_word(system, 0x14U, 0x1000fffaU); // BEQ zero,zero,0.
    rsp_word(system, 0x18U, 0U);          // Delay slot.
    start_rsp(system);
}

} // namespace

TEST(cpu_cached_rsp_window_matches_steps_across_phases_and_early_cpu_exits) {
    constexpr std::array<unsigned, 7> budgets{6U, 7U, 8U, 9U, 13U, 17U, 31U};
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu_with_load_hazard(*system);
            system->advance(phase); // RSP is halted; this selects each CPU/RCP phase.
            prepare_local_branch_program(*system);
        }

        for (const unsigned budget : budgets)
            compare_slice(batched, stepped, budget);

        CHECK(batched.cpu.batched_cached_instructions() > 16U);
        CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
    }
}

TEST(cpu_cached_rsp_window_reauthenticates_same_line_imem_mutation_at_same_pc) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu_with_load_hazard(*system);
            system->advance(phase);
            rsp_word(*system, 0x00U, 0x24210001U); // ADDIU at,at,1.
            rsp_word(*system, 0x04U, 0xac010080U); // SW at,0x80(zero).
            rsp_word(*system, 0x08U, 0U);
            rsp_word(*system, 0x0cU, 0U);
            rsp_word(*system, 0x10U, 0U);
            rsp_word(*system, 0x14U, 0U);
            rsp_word(*system, 0x18U, 0x1000fff9U); // BEQ zero,zero,0.
            rsp_word(*system, 0x1cU, 0U);
            start_rsp(*system);
        }

        compare_slice(batched, stepped, 18U);
        for (auto* system : {&batched, &stepped}) {
            system->rsp.write_pc(0U); // Revisit the same PC with cached line metadata still resident.
            rsp_word(*system, 0x00U, 0x40026000U); // MFC0 v0,DPC_CLOCK: shared now.
            rsp_word(*system, 0x04U, 0xac020084U); // SW v0,0x84(zero).
            rsp_word(*system, 0x08U, 0x0000000dU); // BREAK.
        }

        compare_slice(batched, stepped, 24U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x84U), read_be32(stepped.rsp.memory.data() + 0x84U));
        CHECK(read_be32(batched.rsp.memory.data() + 0x84U) != 0U);
        CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
    }
}

TEST(cpu_cached_rsp_window_keeps_latched_local_words_authoritative_after_imem_mutation) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu_with_load_hazard(*system);
            system->advance(phase);
            rsp_word(*system, 0x00U, 0xc8012000U); // LQV v1,0(zero).
            rsp_word(*system, 0x04U, 0x4a010850U); // VADD v1,v1,v1.
            rsp_word(*system, 0x08U, 0x24020055U); // ADDIU v0,zero,0x55 paired behind the stall.
            rsp_word(*system, 0x0cU, 0xac020080U); // SW v0,0x80(zero).
            rsp_word(*system, 0x10U, 0x24210001U);
            rsp_word(*system, 0x14U, 0x24420001U);
            rsp_word(*system, 0x18U, 0x24630001U);
            rsp_word(*system, 0x1cU, 0x0000000dU); // BREAK after enough future local work for a window.
            start_rsp(*system);
            system->rsp.tick(1); // Retire LQV.
            system->rsp.tick(1); // Latch VADD+ADDIU, then stall on the vector result.
            CHECK_EQ(system->rsp.pc, 4U);
            rsp_word(*system, 0x04U, 0x0000000dU); // Live IMEM no longer matches the latched VADD.
            rsp_word(*system, 0x08U, 0x40026000U); // Nor the latched ADDIU.
        }

        compare_slice(batched, stepped, 64U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), 0x55U);
        CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
    }
}

TEST(cpu_cached_rsp_window_stops_at_first_branch_before_shared_target) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu_with_load_hazard(*system);
            system->advance(phase);
            rsp_word(*system, 0x00U, 0x4a000037U); // VNOP, pairs with the following scalar op.
            rsp_word(*system, 0x04U, 0x24010033U); // ADDIU at,zero,0x33.
            rsp_word(*system, 0x08U, 0x4a000037U); // VNOP.
            rsp_word(*system, 0x0cU, 0x24020044U); // ADDIU v0,zero,0x44.
            rsp_word(*system, 0x10U, 0x4a000037U); // VNOP.
            rsp_word(*system, 0x14U, 0x10000003U); // BEQ zero,zero,0x24; third proven cycle.
            rsp_word(*system, 0x18U, 0xac010080U); // SW at,0x80(zero) in the delay slot.
            rsp_word(*system, 0x1cU, 0x24040077U); // Skipped fallthrough marker.
            rsp_word(*system, 0x20U, 0xac040088U);
            rsp_word(*system, 0x24U, 0x40026000U); // MFC0 v0,DPC_CLOCK at taken target.
            rsp_word(*system, 0x28U, 0xac020084U);
            rsp_word(*system, 0x2cU, 0x0000000dU);
            start_rsp(*system);
        }

        compare_slice(batched, stepped, 64U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), 0x33U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x88U), 0U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x84U), read_be32(stepped.rsp.memory.data() + 0x84U));
        CHECK(read_be32(batched.rsp.memory.data() + 0x84U) != 0U);
    }
}

TEST(cpu_cached_rsp_window_reproves_across_64_byte_and_imem_wrap_boundaries) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (const u32 start : {0x028U, 0x0fe8U}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_cpu_with_load_hazard(*system);
                system->advance(phase);
                const u32 after = (start + 24U) & 0x0ffcU;
                rsp_word(*system, start + 0U, 0x24010011U); // ADDIU at,zero,0x11.
                rsp_word(*system, start + 4U, 0xac010080U); // SW at,0x80(zero).
                rsp_word(*system, start + 8U, 0U);
                rsp_word(*system, start + 12U, 0U);
                rsp_word(*system, start + 16U, 0U);
                rsp_word(*system, start + 20U, 0U);         // Last word in the first proof line.
                rsp_word(*system, after + 0U, 0x24020022U); // ADDIU v0,zero,0x22 after boundary.
                rsp_word(*system, after + 4U, 0xac020084U); // SW v0,0x84(zero).
                rsp_word(*system, after + 8U, 0x0000000dU); // BREAK.
                start_rsp(*system, start);
            }

            compare_slice(batched, stepped, 64U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), 0x11U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x84U), 0x22U);
            CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
        }
    }
}
