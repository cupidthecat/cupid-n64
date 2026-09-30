#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr std::array<u32, 10> program{
    0x24210001U, // ADDIU at,at,1.
    0x0021182dU, // DADDU v1,at,at.
    0x38640055U, // XORI a0,v1,0x55.
    0x00042ff8U, // DSLL a1,a0,31.
    0x00a43025U, // OR a2,a1,a0.
    0x00c63827U, // NOR a3,a2,a2.
    0x2ce8ffffU, // SLTIU t0,a3,-1.
    0x00074803U, // SRA t1,a3,0.
    0x08000400U, // J code.
    0x25290001U, // ADDIU t1,t1,1 in the delay slot.
};

void prepare(System& system, u64 address = code) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
    for (unsigned index = 0; index < program.size(); ++index) {
        const u32 word = index == 8U ? 0x08000000U | (physical >> 2U) : program[index];
        system.bus.write(physical + index * 4U, 4, word);
    }
    system.cpu.set_pc(address);
}

void equivalent(System& native, System& ordinary) {
    native.settle();
    ordinary.settle();
    CHECK(native.cpu.gpr == ordinary.cpu.gpr);
    CHECK(native.cpu.cp0 == ordinary.cpu.cp0);
    CHECK_EQ(native.cpu.pc, ordinary.cpu.pc);
    CHECK_EQ(native.cpu.next_pc, ordinary.cpu.next_pc);
    CHECK_EQ(native.cpu.hi, ordinary.cpu.hi);
    CHECK_EQ(native.cpu.lo, ordinary.cpu.lo);
    CHECK_EQ(native.cpu.cycles, ordinary.cpu.cycles);
    CHECK_EQ(native.cpu.instruction_count, ordinary.cpu.instruction_count);
    CHECK_EQ(native.cpu.exception_pending, ordinary.cpu.exception_pending);
    CHECK_EQ(native.cpu.frozen, ordinary.cpu.frozen);
    CHECK_EQ(native.cpu.linked, ordinary.cpu.linked);
    CHECK_EQ(native.cpu.read_cop0(1), ordinary.cpu.read_cop0(1));
    CHECK_EQ(native.bus.output_clock(), ordinary.bus.output_clock());
    CHECK_EQ(native.rsp.pc, ordinary.rsp.pc);
    CHECK(native.rsp.memory == ordinary.rsp.memory);
    for (unsigned index = 0; index < native.cpu.instruction_cache.size(); ++index) {
        const auto& a = native.cpu.instruction_cache[index];
        const auto& b = ordinary.cpu.instruction_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
    }
}

void compare(System& native, System& ordinary, unsigned steps, u64 cycles = 1000000U) {
    const u64 start = ordinary.cpu.cycles;
    unsigned executed = 0;
    while (executed < steps && ordinary.cpu.cycles - start < cycles && !ordinary.cpu.frozen) {
        ordinary.cpu.step();
        ++executed;
    }
    CHECK_EQ(native.cpu.run_slice(steps, cycles), executed);
    equivalent(native, ordinary);
}

void warm(System& native, System& ordinary) {
    compare(native, ordinary, 1024);
    CHECK_EQ(native.cpu.native_block_instructions() != 0, CpuNativeCode::available());
}

void replace_cached_word(System& system, u64 address, u32 word) {
    auto& line = system.cpu.instruction_cache[(address >> 5U) & 511U];
    CHECK(line.valid);
    write_be32(line.data.data() + (address & 31U), word);
}
} // namespace

TEST(cpu_native_execution_preserves_partial_budgets_and_all_system_clock_phases) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        for (auto* system : {&native, &ordinary}) {
            prepare(*system);
            system->advance(phase);
        }
        warm(native, ordinary);
        for (unsigned budget = 0; budget <= 33U; ++budget) {
            compare(native, ordinary, budget);
            compare(native, ordinary, 65U, budget);
        }
        for (unsigned index = 0; index < 12; ++index)
            compare(native, ordinary, 1);
    }
}

TEST(cpu_native_execution_preserves_count_interrupts_at_every_block_position) {
    for (unsigned distance = 1; distance <= 9U; ++distance) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        prepare(native);
        prepare(ordinary);
        warm(native, ordinary);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + distance);
            system->cpu.write_cop0(12, 0x34008001U);
        }
        compare(native, ordinary, 48);
        CHECK_EQ(native.cpu.cp0[13] & 0x8000U, 0x8000U);
    }
}

TEST(cpu_native_execution_keeps_stale_instructions_until_cache_invalidation) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    for (auto* system : {&native, &ordinary})
        system->bus.write(0x1000U, 4, 0x24210007U);
    compare(native, ordinary, 200);
    const u64 old = native.cpu.gpr[1];
    for (auto* system : {&native, &ordinary})
        system->cpu.cache_operation(0x10U, code);
    compare(native, ordinary, 500);
    CHECK(native.cpu.gpr[1] > old + 300U);
}

TEST(cpu_native_execution_recompiles_changed_cache_images_and_tags) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    for (auto* system : {&native, &ordinary})
        replace_cached_word(*system, code, 0x24210009U);
    compare(native, ordinary, 500);
    for (auto* system : {&native, &ordinary}) {
        const u64 alias = code + 0x4000U;
        prepare(*system, alias);
        system->bus.write(0x5000U, 4, 0x2421000bU);
    }
    compare(native, ordinary, 1500);
    CHECK(native.cpu.native_block_instructions() != 0 || !CpuNativeCode::available());
}

TEST(cpu_native_execution_handles_branch_delay_load_interlocks_and_fallback_transitions) {
    for (unsigned replacement = 0; replacement < 3; ++replacement) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        prepare(native);
        prepare(ordinary);
        warm(native, ordinary);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.gpr[20] = 0xffffffff80003000ULL;
            system->bus.write(0x3000U, 4, 0x7fffffffU);
            const u32 word = replacement == 0   ? 0x8e810000U  // LW at,0(s4).
                             : replacement == 1 ? 0x44010000U  // MFC1 at,f0.
                                                : 0x10000001U; // Branch into the straight-line body.
            replace_cached_word(*system, code, word);
        }
        compare(native, ordinary, 1200);
    }
}

TEST(cpu_native_execution_preserves_cop2_memory_transfers_between_integer_blocks) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    const u64 compiled = native.cpu.native_block_instructions();
    for (auto* system : {&native, &ordinary}) {
        system->cpu.write_cop0(12, 0x74000000U);
        system->cpu.gpr[20] = 0xffffffffa0003000ULL;
        system->cpu.gpr[21] = 0xffffffffa0004000ULL;
        system->bus.write(0x3000U, 8, 0xfedcba9876543210ULL);
        system->bus.write(0x4000U, 8, 0x1111111122222222ULL);
        system->bus.write(0x4008U, 8, 0x3333333344444444ULL);
        // The next cache line holds the fallback operations and returns to the integer block.
        replace_cached_word(*system, code + 32U, (0x32U << 26U) | (20U << 21U) | (9U << 16U) | 4U);
        replace_cached_word(*system, code + 36U, (0x12U << 26U) | (1U << 21U) | (2U << 16U));
        replace_cached_word(*system, code + 40U, (0x3aU << 26U) | (21U << 21U) | (9U << 16U) | 4U);
        replace_cached_word(*system, code + 44U, (0x3eU << 26U) | (21U << 21U) | (10U << 16U) | 8U);
        replace_cached_word(*system, code + 48U, 0x08000400U);
        replace_cached_word(*system, code + 52U, 0U);
    }
    compare(native, ordinary, 1200);
    CHECK_EQ(native.cpu.gpr[2], 0xfedcba9876543210ULL);
    CHECK_EQ(native.cpu.cop2_latch, ordinary.cpu.cop2_latch);
    for (auto* system : {&native, &ordinary}) {
        CHECK_EQ(system->bus.memory.read(0x4000U, 4), 0x11111111U);
        CHECK_EQ(system->bus.memory.read(0x4004U, 4), 0x76543210U);
        CHECK_EQ(system->bus.memory.read(0x4008U, 8), 0xfedcba9876543210ULL);
    }
    CHECK_EQ(native.cpu.native_block_instructions() > compiled, CpuNativeCode::available());
}

TEST(cpu_native_execution_stops_at_device_callbacks_and_live_instruction_edits) {
    using Observation = std::array<u64, 5>;
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    std::array<std::vector<Observation>, 2> records;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    const u64 compiled = native.cpu.native_block_instructions();
    for (unsigned index = 0; index < systems->size(); ++index) {
        auto& system = (*systems)[index];
        system.cpu.write_cop0(12, 0x34000401U);
        system.bus.write(0x0430000cU, 4, 2); // Only the SP interrupt is unmasked at MI.
        system.rsp.write_register(0x10, 1);  // Keep zero-filled local RSP work running.
        system.bus.write(0x04500010U, 4, 99);
        system.bus.set_audio_sample_output([&system, &output = records[index]](const AudioSample&) {
            output.push_back({system.cpu.cycles, system.cpu.pc, system.cpu.gpr[1], system.cpu.read_cop0(9),
                              system.bus.output_clock()});
            if (output.size() == 3U)
                replace_cached_word(system, code, 0x2421000fU);
        });
    }
    compare(native, ordinary, 8192);
    CHECK(records[0].size() > 3U);
    CHECK(records[0] == records[1]);
    CHECK_EQ(native.cpu.native_block_instructions() > compiled, CpuNativeCode::available());
}

TEST(cpu_native_execution_yields_to_interruptible_rsp_instruction_boundaries) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    const u64 compiled = native.cpu.native_block_instructions();
    for (auto* system : {&native, &ordinary}) {
        system->cpu.write_cop0(12, 0x34000401U);
        system->bus.write(0x0430000cU, 4, 2);           // Enable the SP interrupt at MI.
        system->bus.write(0x04001000U, 4, 0x24010010U); // ADDIU at,zero,SET_INTR.
        system->bus.write(0x04001004U, 4, 0x40812000U); // MTC0 at,SP_STATUS.
        system->bus.write(0x04001008U, 4, 0x1000ffffU);
        system->bus.write(0x0400100cU, 4, 0);
        system->rsp.write_register(0x10, 1);
    }
    compare(native, ordinary, 3);
    CHECK_EQ(native.cpu.native_block_instructions() > compiled, CpuNativeCode::available());
    compare(native, ordinary, 100);
    CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
}

TEST(cpu_native_execution_commits_only_the_prefix_before_rsp_interrupts) {
    std::array<unsigned, 3> interrupt_positions{};
    for (unsigned local_prefix = 0; local_prefix < interrupt_positions.size(); ++local_prefix) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        prepare(native);
        prepare(ordinary);
        warm(native, ordinary);
        ordinary.cpu.set_pc(code);
        ordinary.cpu.write_cop0(12, 0x34000401U);
        ordinary.bus.write(0x0430000cU, 4, 2);
        for (unsigned index = 0; index < local_prefix; ++index)
            ordinary.bus.write(0x04001000U + index * 4U, 4, 0x24210001U);
        ordinary.bus.write(0x04001000U + local_prefix * 4U, 4, 0x0000000dU);
        ordinary.rsp.write_register(0x10, 0x101U);
        unsigned retired = 0;
        while (!ordinary.cpu.exception_pending && retired <= CpuNativeCode::maximum_instructions) {
            ordinary.cpu.step();
            ++retired;
        }
        CHECK(ordinary.cpu.exception_pending);
        CHECK(retired <= CpuNativeCode::maximum_instructions);
        interrupt_positions[local_prefix] = retired;

        for (auto* system : {&native, &ordinary}) {
            system->reset();
            prepare(*system);
        }
        warm(native, ordinary);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.set_pc(code);
            system->cpu.write_cop0(12, 0x34000401U);
            system->bus.write(0x0430000cU, 4, 2); // Enable the SP interrupt at MI.
            for (unsigned index = 0; index < local_prefix; ++index)
                system->bus.write(0x04001000U + index * 4U, 4, 0x24210001U);    // ADDIU at,at,1.
            system->bus.write(0x04001000U + local_prefix * 4U, 4, 0x0000000dU); // BREAK.
            // Clear HALT and request an interrupt when BREAK retires.
            system->rsp.write_register(0x10, 0x101U);
        }
        compare(native, ordinary, CpuNativeCode::maximum_instructions);
        CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
    }
    CHECK(interrupt_positions[0] < interrupt_positions[1]);
    CHECK(interrupt_positions[1] < interrupt_positions[2]);
}

TEST(cpu_native_execution_keeps_blocks_when_interruptible_rsp_work_is_local) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    const u64 compiled = native.cpu.native_block_instructions();
    for (auto* system : {&native, &ordinary}) {
        system->cpu.write_cop0(12, 0x34000401U);
        system->bus.write(0x0430000cU, 4, 2); // Enable the SP interrupt at MI.
        // Zero-filled IMEM is an unbounded stream of local SLL zero,zero,0 work.
        system->rsp.write_register(0x10, 1);
    }
    compare(native, ordinary, 2000);
    CHECK_EQ(native.cpu.native_block_instructions() > compiled, CpuNativeCode::available());
}

TEST(cpu_native_execution_releases_cached_code_on_reset_and_obeys_runtime_selection) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    const u64 compiled = native.cpu.native_block_instructions();
    native.cpu.set_native_execution(false);
    compare(native, ordinary, 2000);
    CHECK_EQ(native.cpu.native_block_instructions(), compiled);
    native.cpu.set_native_execution(true);
    compare(native, ordinary, 2000);
    CHECK_EQ(native.cpu.native_block_instructions() > compiled, CpuNativeCode::available());
    native.reset();
    ordinary.reset();
    CHECK_EQ(native.cpu.native_block_instructions(), 0U);
    prepare(native);
    prepare(ordinary);
    compare(native, ordinary, 1024);
    CHECK_EQ(native.cpu.native_block_instructions() != 0, CpuNativeCode::available());
}
