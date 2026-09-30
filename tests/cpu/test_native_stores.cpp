#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 data = 0xffffffff80003000ULL;

constexpr u32 memory(unsigned opcode, unsigned base, unsigned source, s16 offset) {
    return (opcode << 26U) | (base << 21U) | (source << 16U) | static_cast<u16>(offset);
}

void install_line(System& system, u64 address = data) {
    auto& line = system.cpu.data_cache[(address >> 4U) & 511U];
    line = {};
    line.valid = true;
    line.tag = static_cast<u32>(address) & 0x1ffff000U;
    for (unsigned byte = 0; byte < 16U; ++byte)
        line.data[byte] = static_cast<u8>(0x80U + byte);
}

constexpr std::array program{
    0x24210001U,             // ADDIU at,at,1.
    memory(0x2bU, 20, 1, 0), // SW at,0(s4).
    0x24420003U,             // ADDIU v0,v0,3.
    memory(0x28U, 20, 2, 1), // SB v0,1(s4), overlapping SW.
    memory(0x29U, 20, 1, 2), // SH at,2(s4), overlapping SW.
    memory(0x3fU, 20, 0, 8), // SD zero,8(s4).
    0x24630005U,             // ADDIU v1,v1,5.
    memory(0x23U, 20, 4, 0), // LW a0,0(s4) sees the committed stores.
    0x08000400U,             // J code.
    0U,
};

void prepare(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    for (unsigned index = 0; index < program.size(); ++index)
        system.bus.write(0x1000U + index * 4U, 4, program[index]);
    install_line(system);
    system.cpu.gpr[20] = data;
    system.cpu.set_pc(code);
}

void compare(System& native, System& ordinary, unsigned maximum_steps, u64 budget = 1000000U) {
    const u64 start = ordinary.cpu.cycles;
    unsigned steps = 0;
    while (steps < maximum_steps && ordinary.cpu.cycles - start < budget && !ordinary.cpu.frozen) {
        ordinary.cpu.step();
        ++steps;
    }
    CHECK_EQ(native.cpu.run_slice(maximum_steps, budget), steps);
    native.settle();
    ordinary.settle();
    CHECK(native.cpu.gpr == ordinary.cpu.gpr);
    CHECK(native.cpu.cp0 == ordinary.cpu.cp0);
    CHECK_EQ(native.cpu.pc, ordinary.cpu.pc);
    CHECK_EQ(native.cpu.next_pc, ordinary.cpu.next_pc);
    CHECK_EQ(native.cpu.cycles, ordinary.cpu.cycles);
    CHECK_EQ(native.cpu.instruction_count, ordinary.cpu.instruction_count);
    CHECK_EQ(native.cpu.exception_pending, ordinary.cpu.exception_pending);
    CHECK_EQ(native.bus.output_clock(), ordinary.bus.output_clock());
    CHECK_EQ(native.rsp.pc, ordinary.rsp.pc);
    CHECK(native.rsp.memory == ordinary.rsp.memory);
    for (unsigned index = 0; index < native.cpu.data_cache.size(); ++index) {
        const auto& a = native.cpu.data_cache[index];
        const auto& b = ordinary.cpu.data_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
        CHECK_EQ(a.dirty, b.dirty);
    }
}

void warm(System& native, System& ordinary) {
    compare(native, ordinary, 1024U);
    CHECK_EQ(native.cpu.native_block_instructions() != 0, CpuNativeCode::available());
}
} // namespace

TEST(cpu_native_store_values_preserve_width_endianness_offsets_aliases_and_zero) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    for (unsigned opcode : {0x28U, 0x29U, 0x2bU, 0x3fU}) {
        const unsigned width = opcode == 0x28U ? 1U : opcode == 0x29U ? 2U : opcode == 0x2bU ? 4U : 8U;
        for (unsigned source : {0U, 1U, 2U}) {
            for (unsigned offset = 0; offset < 16U; offset += width) {
                const u32 word = memory(opcode, 1, source, static_cast<s16>(offset) - 16);
                CHECK(CpuNativeCode::supports(word));
                const auto compiled = CpuNativeCode::compile(std::span(&word, 1));
                CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
                if (!compiled)
                    continue;
                for (auto* system : {&native, &ordinary}) {
                    prepare(*system);
                    system->cpu.gpr[1] = data + 16U;
                    system->cpu.gpr[2] = 0xfedcba9876543210ULL;
                }
                const auto before = native.cpu.data_cache[(data >> 4U) & 511U];
                ordinary.cpu.execute(word);
                CpuNativeState state{native.cpu.gpr.data(), native.cpu.data_cache.data()};
                CHECK(compiled->execute(state));
                const auto& a = native.cpu.data_cache[(data >> 4U) & 511U];
                const auto& b = ordinary.cpu.data_cache[(data >> 4U) & 511U];
                CHECK(a.data == b.data);
                CHECK(a.dirty);
                CHECK(native.cpu.gpr == ordinary.cpu.gpr);
                state.rollback_stores();
                CHECK(a.data == before.data);
                CHECK_EQ(a.dirty, before.dirty);
            }
        }
    }
}

TEST(cpu_native_stores_commit_overlaps_in_order_and_rollback_dirty_lines_in_reverse) {
    const std::array words{memory(0x3fU, 1, 2, 0), memory(0x2bU, 1, 3, 0), memory(0x29U, 1, 4, 2),
                           memory(0x28U, 1, 0, 3)};
    const auto compiled = CpuNativeCode::compile(words);
    CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
    if (!compiled)
        return;
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    for (bool dirty : {false, true}) {
        for (auto* system : {&native, &ordinary}) {
            prepare(*system);
            system->cpu.data_cache[(data >> 4U) & 511U].dirty = dirty;
            system->cpu.gpr[1] = data;
            system->cpu.gpr[2] = 0xfedcba9876543210ULL;
            system->cpu.gpr[3] = 0x12345678U;
            system->cpu.gpr[4] = 0xabcdU;
        }
        const auto before = native.cpu.data_cache[(data >> 4U) & 511U];
        for (u32 word : words)
            ordinary.cpu.execute(word);
        CpuNativeState state{native.cpu.gpr.data(), native.cpu.data_cache.data()};
        CHECK(compiled->execute(state));
        CHECK_EQ(state.store_count, 4U);
        const auto& line = native.cpu.data_cache[(data >> 4U) & 511U];
        CHECK(line.data == ordinary.cpu.data_cache[(data >> 4U) & 511U].data);
        CHECK_EQ(read_be64(line.data.data()), 0x1234ab0076543210ULL);
        state.rollback_stores();
        CHECK(line.data == before.data);
        CHECK_EQ(line.dirty, dirty);
    }
}

TEST(cpu_native_stores_guard_all_addresses_before_committing_any_cache_bytes) {
    const std::array words{memory(0x2bU, 1, 2, 0), 0x24210010U, memory(0x2bU, 1, 2, 0)};
    const auto compiled = CpuNativeCode::compile(words);
    CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
    if (!compiled)
        return;
    auto machine = std::make_unique<System>();
    for (unsigned failure = 0; failure < 5U; ++failure) {
        prepare(*machine);
        machine->cpu.gpr[1] = failure == 0U ? data + 1U : failure == 1U ? 0xffffffffa0003000ULL : data;
        machine->cpu.gpr[2] = 0x12345678U;
        if (failure == 3U) {
            install_line(*machine, data + 16U);
            ++machine->cpu.data_cache[((data + 16U) >> 4U) & 511U].tag;
        }
        const auto before = machine->cpu.data_cache;
        CpuNativeState state{machine->cpu.gpr.data(),
                             failure == 4U ? nullptr : machine->cpu.data_cache.data()};
        CHECK(!compiled->execute(state));
        CHECK_EQ(state.store_count, 0U);
        for (unsigned index = 0; index < before.size(); ++index) {
            CHECK(machine->cpu.data_cache[index].data == before[index].data);
            CHECK_EQ(machine->cpu.data_cache[index].dirty, before[index].dirty);
        }
    }
}

TEST(cpu_native_stores_stop_before_loads_that_need_store_forwarding) {
    const std::array words{memory(0x2bU, 1, 2, 0), memory(0x23U, 1, 3, 0)};
    CHECK(!CpuNativeCode::compile(words));
    const std::array permitted{memory(0x23U, 1, 3, 0), memory(0x2bU, 1, 3, 4)};
    CHECK_EQ(CpuNativeCode::compile(permitted) != nullptr, CpuNativeCode::available());
}

TEST(cpu_native_store_blocks_preserve_partial_budgets_clock_phases_and_following_loads) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        for (auto* system : {&native, &ordinary}) {
            prepare(*system);
            system->advance(phase);
        }
        warm(native, ordinary);
        for (unsigned budget = 0; budget <= 25U; ++budget) {
            compare(native, ordinary, 30U, budget);
            compare(native, ordinary, budget);
        }
    }
}

TEST(cpu_native_store_blocks_restore_registers_and_leave_misses_to_ordinary_execution) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    prepare(native);
    prepare(ordinary);
    warm(native, ordinary);
    for (auto* system : {&native, &ordinary}) {
        system->cpu.set_pc(code);
        system->cpu.gpr[1] = 100U;
        system->cpu.gpr[2] = 200U;
        system->cpu.data_cache[(data >> 4U) & 511U].valid = false;
    }
    compare(native, ordinary, 7U);
    CHECK_EQ(native.cpu.gpr[1], 101U);
    CHECK_EQ(native.cpu.gpr[2], 203U);
}

TEST(cpu_native_store_blocks_replay_only_the_prefix_retired_before_rsp_interrupts) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (unsigned prefix = 0; prefix < 5U; ++prefix) {
            auto machines = std::make_unique<std::array<System, 2>>();
            auto& [native, ordinary] = *machines;
            prepare(native);
            prepare(ordinary);
            warm(native, ordinary);
            for (auto* system : {&native, &ordinary}) {
                system->cpu.set_pc(code);
                system->cpu.step();
                system->advance(phase);
                system->cpu.write_cop0(12, 0x34000401U);
                system->bus.write(0x0430000cU, 4, 2U);
                for (unsigned index = 0; index < prefix; ++index)
                    system->bus.write(0x04001000U + index * 4U, 4, 0x24210001U);
                system->bus.write(0x04001000U + prefix * 4U, 4, 0x0000000dU);
                system->rsp.write_pc(0);
                system->rsp.write_register(0x10U, 0x101U);
            }
            compare(native, ordinary, 12U);
            CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
        }
    }
}

TEST(cpu_native_store_blocks_stop_before_count_compare_interrupts) {
    for (unsigned distance = 1; distance < 10U; ++distance) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        prepare(native);
        prepare(ordinary);
        warm(native, ordinary);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + distance);
            system->cpu.write_cop0(12, 0x34008001U);
        }
        compare(native, ordinary, 30U);
        CHECK_EQ(native.cpu.cp0[13] & 0x8000U, 0x8000U);
    }
}

TEST(cpu_native_store_blocks_discard_staged_writes_when_a_later_line_misses) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    const std::array words{
        0x24210001U, memory(0x2bU, 20, 1, 0), 0x24420003U, memory(0x2bU, 20, 2, 16), 0x08000400U, 0U};
    for (auto* system : {&native, &ordinary}) {
        prepare(*system);
        for (unsigned index = 0; index < words.size(); ++index)
            system->bus.write(0x1000U + index * 4U, 4, words[index]);
        install_line(*system, data + 16U);
    }
    warm(native, ordinary);
    for (auto* system : {&native, &ordinary}) {
        system->cpu.set_pc(code);
        system->cpu.step();
        system->cpu.gpr[1] = 100U;
        system->cpu.gpr[2] = 200U;
        system->cpu.data_cache[((data + 16U) >> 4U) & 511U].valid = false;
    }
    compare(native, ordinary, 3U);
    CHECK_EQ(native.cpu.gpr[1], 100U);
    CHECK_EQ(native.cpu.gpr[2], 203U);
    CHECK_EQ(read_be32(native.cpu.data_cache[(data >> 4U) & 511U].data.data()), 100U);
}

TEST(cpu_native_store_blocks_keep_rsp_dma_reads_on_backing_memory_until_writeback) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        prepare(native);
        prepare(ordinary);
        warm(native, ordinary);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.set_pc(code);
            system->cpu.step();
            system->advance(phase);
            system->bus.write(0x3000U, 8, 0xfedcba9876543210ULL);
            system->bus.write(0x3008U, 8, 0x0123456789abcdefULL);
            system->rsp.write_register(0, 0x200U);
            system->rsp.write_register(4, 0x3000U);
            system->rsp.write_register(8, 15U);
        }
        compare(native, ordinary, 128U);
        CHECK_EQ(read_be64(native.rsp.memory.data() + 0x200U), 0xfedcba9876543210ULL);
        CHECK_EQ(read_be64(native.rsp.memory.data() + 0x208U), 0x0123456789abcdefULL);
        for (auto* system : {&native, &ordinary})
            system->cpu.cache_operation(0x19U, data);
        CHECK_EQ(native.bus.memory.read(0x3000U, 8), ordinary.bus.memory.read(0x3000U, 8));
        CHECK_EQ(native.bus.memory.read(0x3008U, 8), 0U);
    }
}
