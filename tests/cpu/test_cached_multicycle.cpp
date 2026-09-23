#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <bit>
#include <initializer_list>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 data = 0xffffffff80002000ULL;
constexpr u32 flush_subnormals = 1U << 24U;

constexpr u32 immediate(unsigned op, unsigned rs, unsigned rt, int value) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

constexpr u32 cop1_format(unsigned format, unsigned ft, unsigned fs, unsigned fd, unsigned function) {
    return (0x11U << 26U) | (format << 21U) | (ft << 16U) | (fs << 11U) | (fd << 6U) | function;
}

void write_program(System& system, std::initializer_list<u32> instructions) {
    u32 address = static_cast<u32>(code) & 0x1fffffffU;
    for (const u32 instruction : instructions) {
        system.bus.write(address, 4, instruction);
        address += 4U;
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

void prepare(System& system, std::initializer_list<u32> instructions, bool full_registers = true) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x30000000U | (full_registers ? 0x04000000U : 0U));
    write_program(system, instructions);
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system, code);
}

void latch_prologue(System& system) {
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
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
    CHECK_EQ(batched.cpu.exception_pending, stepped.cpu.exception_pending);
    CHECK_EQ(batched.cpu.frozen, stepped.cpu.frozen);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK(batched.cpu.fpu.registers == stepped.cpu.fpu.registers);
    CHECK_EQ(batched.cpu.fpu.control, stepped.cpu.fpu.control);
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

void short_video(System& system) {
    auto& bus = system.bus;
    bus.write(0x04400000, 4, 0x303);
    bus.write(0x04400004, 4, 0x3000);
    bus.write(0x04400008, 4, 16);
    bus.write(0x0440000c, 4, 4);
    bus.write(0x04400018, 4, 13);
    bus.write(0x0440001c, 4, 99);
    bus.write(0x04400020, 4, (100U << 16U) | 100U);
    bus.write(0x04400024, 4, (108U << 16U) | 111U);
    bus.write(0x04400028, 4, (2U << 16U) | 4U);
    bus.write(0x04400030, 4, 1024);
    bus.write(0x04400034, 4, 1024);
}

void rsp_word(System& system, u32 address, u32 word) {
    write_be32(system.rsp.memory.data() + 0x1000U + (address & 0x0ffcU), word);
}

void start_rsp(System& system) {
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 1U);
}

void fill_local_rsp_line(System& system) {
    for (u32 address = 0; address < 64U; address += 4U)
        rsp_word(system, address, 0U);
    start_rsp(system);
}

} // namespace

TEST(cpu_cached_multicycle_chains_binary_fpu_results_with_exact_issue_waits) {
    const auto program = {
        0U,
        cop1_format(0x10U, 4, 2, 6, 0),   // ADD.S f6,f2,f4: 3 cycles.
        cop1_format(0x10U, 4, 6, 8, 2),   // MUL.S f8,f6,f4: 5 + 1 issue wait.
        cop1_format(0x10U, 6, 8, 10, 1),  // SUB.S f10,f8,f6: 3 + 1 issue wait.
        cop1_format(0x10U, 2, 10, 12, 3), // DIV.S f12,f10,f2: 29 + 1 issue wait.
        immediate(0x0d, 0, 8, 7),         // ORI t0,zero,7: 1 cycle.
        0x40094800U,                      // MFC0 t1,Count: cached-batch exit.
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, program);
        system->cpu.fpu.control = flush_subnormals;
        system->cpu.fpu.registers[2] = std::bit_cast<u32>(2.25f);
        system->cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
        latch_prologue(*system);
    }

    const u64 before_cycles = batched.cpu.cycles;
    const u64 before_instructions = batched.cpu.instruction_count;
    const u64 before_batched = batched.cpu.batched_cached_instructions();
    compare_slice(batched, stepped, 5U);

    CHECK_EQ(batched.cpu.cycles - before_cycles, 44U);
    CHECK_EQ(batched.cpu.instruction_count - before_instructions, 5U);
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before_batched, 5U);
    CHECK((batched.cpu.cycles - before_cycles) != (batched.cpu.instruction_count - before_instructions));
    CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[6]), std::bit_cast<u32>(5.75f));
    CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[8]), std::bit_cast<u32>(20.125f));
    CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[10]), std::bit_cast<u32>(14.375f));
    CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[12]), std::bit_cast<u32>(14.375f / 2.25f));
    CHECK_EQ(batched.cpu.gpr[8], 7U);
}

TEST(cpu_cached_multicycle_preserves_encoded_load_interlock_and_cycle_budgets) {
    const auto program = {
        0U,
        immediate(0x23, 16, 8, 0),      // LW t0,0(s0): 1 cycle.
        immediate(0x09, 0, 8, 1),       // ADDIU t0,zero,1: encoded-rt wait makes 2 cycles.
        cop1_format(0x10U, 4, 2, 6, 3), // DIV.S f6,f2,f4: 29 cycles.
        immediate(0x0d, 0, 9, 9),       // ORI t1,zero,9: 1 cycle.
        0x400a4800U,
    };
    struct Boundary {
        u64 budget;
        unsigned steps;
        u64 cycles;
    };
    constexpr std::array boundaries{Boundary{1U, 1U, 1U},  Boundary{2U, 2U, 3U},   Boundary{3U, 2U, 3U},
                                    Boundary{4U, 3U, 32U}, Boundary{31U, 3U, 32U}, Boundary{32U, 3U, 32U},
                                    Boundary{33U, 4U, 33U}};
    for (const auto boundary : boundaries) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, program);
            warm_data_cache(*system);
            system->cpu.gpr[16] = data;
            system->cpu.fpu.control = flush_subnormals;
            system->cpu.fpu.registers[2] = std::bit_cast<u32>(2.5f);
            system->cpu.fpu.registers[4] = std::bit_cast<u32>(2.5f);
            latch_prologue(*system);
        }
        const u64 before_cycles = batched.cpu.cycles;
        const u64 before_instructions = batched.cpu.instruction_count;
        compare_slice(batched, stepped, 4U, boundary.budget);
        CHECK_EQ(batched.cpu.cycles - before_cycles, boundary.cycles);
        CHECK_EQ(batched.cpu.instruction_count - before_instructions, boundary.steps);
        CHECK_EQ(batched.cpu.gpr[8], boundary.steps >= 2U ? 1U : 0x11223344U);
        if (boundary.steps >= 3U)
            CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[6]), std::bit_cast<u32>(1.0f));
        if (boundary.steps == 4U)
            CHECK_EQ(batched.cpu.gpr[9], 9U);
    }
}

TEST(cpu_cached_multicycle_respects_fr_rounding_and_arithmetic_trap_fallbacks) {
    for (const bool full_registers : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, cop1_format(0x10U, 5, 3, 7, 0), immediate(0x0d, 0, 8, 1), 0x40094800U},
                    full_registers);
            system->cpu.fpu.control = flush_subnormals;
            system->cpu.fpu.registers[2] = std::bit_cast<u32>(1.25f);
            system->cpu.fpu.registers[3] = std::bit_cast<u32>(4.25f);
            system->cpu.fpu.registers[5] = std::bit_cast<u32>(2.0f);
            latch_prologue(*system);
        }
        compare_slice(batched, stepped, 2U);
        const float expected = full_registers ? 6.25f : 3.25f;
        CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[7]), std::bit_cast<u32>(expected));
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
    }

    constexpr std::array<u32, 4> rounded{0x3f800000U, 0x3f800000U, 0x3f800001U, 0x3f800000U};
    for (unsigned rounding = 0; rounding < 4U; ++rounding) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, cop1_format(0x10U, 4, 2, 6, 0), immediate(0x0d, 0, 8, 1), 0x40094800U});
            system->cpu.fpu.control = flush_subnormals | rounding;
            system->cpu.fpu.registers[2] = 0x3f800000U;
            system->cpu.fpu.registers[4] = 0x33800000U; // 2^-24, halfway between adjacent values at 1.0.
            latch_prologue(*system);
        }
        compare_slice(batched, stepped, 2U);
        CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[6]), rounded[rounding]);
    }

    System trap_batched, trap_stepped;
    for (auto* system : {&trap_batched, &trap_stepped}) {
        prepare(*system, {0U, cop1_format(0x10U, 4, 2, 6, 3), immediate(0x0d, 0, 8, 1)});
        system->cpu.fpu.control = flush_subnormals | (1U << 10U); // Enable divide-by-zero exception.
        system->cpu.fpu.registers[2] = 0x3f800000U;
        system->cpu.fpu.registers[4] = 0U;
        system->cpu.fpu.registers[6] = 0x12345678U;
        latch_prologue(*system);
    }
    const u64 before_batched = trap_batched.cpu.batched_cached_instructions();
    const u64 before_cycles = trap_batched.cpu.cycles;
    compare_slice(trap_batched, trap_stepped, 2U, 2U);
    CHECK_EQ(trap_batched.cpu.batched_cached_instructions(), before_batched);
    CHECK_EQ(trap_batched.cpu.cycles - before_cycles,
             6U); // Issue, zero-operand latency, and exception entry.
    CHECK_EQ(static_cast<u32>(trap_batched.cpu.fpu.registers[6]), 0x12345678U);
    CHECK_EQ(trap_batched.cpu.cp0[13] & 0x7cU, static_cast<u32>(Exception::FloatingPoint) << 2U);
}

TEST(cpu_cached_multicycle_keeps_count_compare_and_video_callback_boundaries) {
    {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, cop1_format(0x10U, 4, 2, 6, 2), immediate(0x0d, 0, 8, 1), 0x40094800U});
            system->cpu.fpu.control = flush_subnormals;
            system->cpu.fpu.registers[2] = std::bit_cast<u32>(2.25f);
            system->cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
            latch_prologue(*system);
            system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + 2U);
            system->cpu.write_cop0(12, 0x34008001U);
        }
        const u64 before = batched.cpu.cycles;
        compare_slice(batched, stepped, 2U, 64U);
        CHECK_EQ(batched.cpu.cycles - before, 10U); // Five-cycle MUL.S, then five-cycle interrupt acceptance.
        CHECK((batched.cpu.cp0[13] & 0x8000U) != 0U);
        CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, static_cast<u32>(Exception::Interrupt) << 2U);
    }

    using Observation = std::array<u64, 6>;
    System batched, stepped;
    std::vector<Observation> first, second;
    const auto attach = [&](System& system, std::vector<Observation>& output) {
        prepare(system, {0U, cop1_format(0x10U, 4, 2, 6, 2), cop1_format(0x10U, 4, 2, 8, 0),
                         immediate(0x05, 8, 0, -3), 0U});
        system.cpu.fpu.control = flush_subnormals;
        system.cpu.fpu.registers[2] = std::bit_cast<u32>(2.25f);
        system.cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
        system.cpu.gpr[8] = 1U;
        latch_prologue(system);
        short_video(system);
        system.bus.set_video_output([machine = &system, observations = &output](VideoField) {
            observations->push_back({machine->cpu.cycles, machine->cpu.instruction_count, machine->cpu.pc,
                                     machine->cpu.cp0[9], machine->bus.output_clock(),
                                     static_cast<u32>(machine->cpu.fpu.registers[6])});
        });
    };
    attach(batched, first);
    attach(stepped, second);
    compare_slice(batched, stepped, 4096U);
    CHECK(!first.empty());
    CHECK(first == second);
}

TEST(cpu_cached_multicycle_keeps_shared_rsp_cycles_and_active_dma_boundaries) {
    const auto cpu_program = {0U, cop1_format(0x10U, 4, 2, 6, 2), immediate(0x0d, 0, 8, 1), 0x40094800U};
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, cpu_program);
            system->cpu.write_cop0(12, 0x34000401U);
            system->cpu.fpu.control = flush_subnormals;
            system->cpu.fpu.registers[2] = std::bit_cast<u32>(2.25f);
            system->cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
            latch_prologue(*system);
            system->advance(phase);
            fill_local_rsp_line(*system);
        }
        const u64 before = batched.cpu.batched_cached_instructions();
        compare_slice(batched, stepped, 2U);
        CHECK_EQ(batched.cpu.batched_cached_instructions() - before, 2U);
        CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[6]), std::bit_cast<u32>(7.875f));
    }

    for (unsigned mode = 0; mode < 3U; ++mode) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, cpu_program);
            system->cpu.write_cop0(12, 0x34000401U);
            system->cpu.fpu.control = flush_subnormals;
            system->cpu.fpu.registers[2] = std::bit_cast<u32>(2.25f);
            system->cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
            latch_prologue(*system);
            if (mode == 0) {
                rsp_word(*system, 0, 0x40026000U); // Shared DPC_CLOCK is sampled on its exact RSP cycle.
                rsp_word(*system, 4, 0xac020080U);
                rsp_word(*system, 8, 0x0000000dU);
                start_rsp(*system);
            } else if (mode == 1) {
                fill_local_rsp_line(*system);
                system->bus.write(0x2000U, 4, 0x12345678U);
                system->rsp.write_register(0x00, 0x200U);
                system->rsp.write_register(0x04, 0x2000U);
                system->rsp.write_register(0x08, 31U); // Active DMA keeps slice entry on ordinary stepping.
            } else {
                rsp_word(*system, 0x00U, 0xc8012000U); // LQV v1,0(zero).
                rsp_word(*system, 0x04U, 0x4a010850U); // VADD v1,v1,v1.
                rsp_word(*system, 0x08U, 0x24020055U); // ADDIU v0,zero,0x55, latched with VADD.
                for (u32 address = 0x0cU; address < 64U; address += 4U)
                    rsp_word(*system, address, 0U);
                start_rsp(*system);
                system->rsp.tick(1);
                system->rsp.tick(1);
                CHECK_EQ(system->rsp.pc, 4U);
                rsp_word(*system, 0x04U, 0x0000000dU); // Live IMEM changes after fetch.
                rsp_word(*system, 0x08U, 0x40026000U);
            }
        }
        const u64 before = batched.cpu.batched_cached_instructions();
        compare_slice(batched, stepped, 2U);
        if (mode == 1U)
            CHECK_EQ(batched.cpu.batched_cached_instructions(), before);
        else
            CHECK(batched.cpu.batched_cached_instructions() > before);
        CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[6]), std::bit_cast<u32>(7.875f));
    }
}
