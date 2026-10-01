#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>
#include <span>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 data = 0xffffffff80003000ULL;
constexpr std::array<u8, 16> pattern{
    0x80, 0x7f, 0xff, 0x01, 0x12, 0x34, 0x56, 0x78, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
};

constexpr u32 immediate(unsigned opcode, unsigned rs, unsigned rt, s16 value) {
    return (opcode << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

constexpr u32 special(unsigned function, unsigned rs, unsigned rt, unsigned rd) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | function;
}

void install_data_line(System& system, u64 address = data, bool backing = true) {
    const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
    auto& line = system.cpu.data_cache[(address >> 4U) & 511U];
    line = {};
    line.data = pattern;
    line.tag = physical & 0xfffff000U;
    line.valid = true;
    if (backing) {
        system.bus.write(physical, 8, read_be64(pattern.data()));
        system.bus.write(physical + 8U, 8, read_be64(pattern.data() + 8U));
    }
}

void prepare(System& system, std::span<const u32> words) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    const u32 physical = static_cast<u32>(code) & 0x1fffffffU;
    for (unsigned index = 0; index < words.size(); ++index)
        system.bus.write(physical + index * 4U, 4, words[index]);
    install_data_line(system);
    system.cpu.gpr[20] = data;
    system.cpu.set_pc(code);
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

constexpr std::array<u32, 10> interlock_program{
    immediate(0x23, 20, 2, 0), // LW v0,0(s4).
    special(0x21, 2, 0, 3),    // ADDU v1,v0,zero. One load-use wait.
    immediate(0x24, 20, 4, 4), // LBU a0,4(s4).
    special(0x21, 3, 4, 5),    // ADDU a1,v1,a0. One load-use wait.
    immediate(0x21, 20, 6, 6), // LH a2,6(s4).
    special(0x25, 5, 6, 7),    // OR a3,a1,a2. One load-use wait.
    immediate(0x27, 20, 8, 8), // LWU t0,8(s4). Leaves the load scoreboard live.
    special(0x21, 8, 0, 9),    // ADDU t1,t0,zero. Ordinary slot sees the final wait.
    0x08000400U,               // J code.
    0U,
};

} // namespace

TEST(cpu_native_integer_load_emission_matches_cached_big_endian_values) {
    struct Case {
        unsigned opcode;
        s16 offset;
    };
    constexpr std::array cases{
        Case{0x20, 0}, Case{0x24, 0}, Case{0x21, 0}, Case{0x25, 0},
        Case{0x23, 0}, Case{0x27, 0}, Case{0x37, 8},
    };
    System machine;
    test::initialize_memory(machine);
    machine.cpu.write_cop0(12, 0x34000000U);
    install_data_line(machine);
    for (const auto test_case : cases) {
        const u32 word = immediate(test_case.opcode, 1, 2, test_case.offset);
        CHECK(CpuNativeCode::supports(word));
        const auto compiled = CpuNativeCode::compile(std::span(&word, 1));
        CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
        if (!compiled)
            continue;
        machine.cpu.gpr.fill(0);
        machine.cpu.gpr[1] = data;
        auto native = machine.cpu.gpr;
        machine.cpu.execute(word);
        CpuNativeState state{native.data(), machine.cpu.data_cache.data()};
        CHECK(compiled->execute(state));
        CHECK(native == machine.cpu.gpr);
    }
}

TEST(cpu_native_integer_load_guards_reject_nonhits_before_cache_bytes) {
    const u32 lw = immediate(0x23, 1, 2, 0);
    const u32 lh = immediate(0x21, 1, 2, 0);
    const u32 ld = immediate(0x37, 1, 2, 0);
    const auto compiled_lw = CpuNativeCode::compile(std::span(&lw, 1));
    const auto compiled_lh = CpuNativeCode::compile(std::span(&lh, 1));
    const auto compiled_ld = CpuNativeCode::compile(std::span(&ld, 1));
    CHECK_EQ(compiled_lw != nullptr, CpuNativeCode::available());
    CHECK_EQ(compiled_lh != nullptr, CpuNativeCode::available());
    CHECK_EQ(compiled_ld != nullptr, CpuNativeCode::available());
    if (!compiled_lw || !compiled_lh || !compiled_ld)
        return;

    System machine;
    test::initialize_memory(machine);
    install_data_line(machine);
    std::array<u64, 32> registers{};
    registers[1] = data;
    CpuNativeState state{registers.data(), nullptr};
    CHECK(!compiled_lw->execute(state));

    state.data_cache = machine.cpu.data_cache.data();
    registers[1] = data + 1U;
    CHECK(!compiled_lh->execute(state));
    registers[1] = 0xffffffffa0003000ULL;
    CHECK(!compiled_lw->execute(state));

    auto& line = machine.cpu.data_cache[(data >> 4U) & 511U];
    registers[1] = data;
    line.valid = false;
    CHECK(!compiled_lw->execute(state));
    line.valid = true;
    ++line.tag;
    CHECK(!compiled_lw->execute(state));

    constexpr u64 device = 0xffffffff84000000ULL;
    install_data_line(machine, device, false);
    registers[1] = device;
    CHECK(!compiled_ld->execute(state));
    CHECK(compiled_lw->execute(state));
}

TEST(cpu_native_loads_into_zero_discard_results_and_preserve_access_guards) {
    constexpr std::array<unsigned, 7> opcodes{0x20U, 0x21U, 0x23U, 0x24U, 0x25U, 0x27U, 0x37U};
    auto machine = std::make_unique<System>();
    test::initialize_memory(*machine);
    machine->cpu.write_cop0(12, 0x34000000U);
    install_data_line(*machine);
    for (const unsigned opcode : opcodes) {
        const std::array program{
            immediate(opcode, 1, 0, 0),
            special(0x2dU, 0, 0, 2), // DADDU v0,zero,zero.
            immediate(0x09U, 0, 3, 1),
        };
        const auto compiled = CpuNativeCode::compile(program);
        CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
        if (!compiled)
            continue;
        machine->cpu.gpr.fill(0);
        machine->cpu.gpr[1] = data;
        auto registers = machine->cpu.gpr;
        for (const u32 word : program)
            machine->cpu.execute(word);
        CpuNativeState state{registers.data(), machine->cpu.data_cache.data()};
        CHECK(compiled->execute(state));
        CHECK(registers == machine->cpu.gpr);
        CHECK_EQ(registers[0], 0U);
        CHECK_EQ(registers[2], 0U);
        CHECK_EQ(registers[3], 1U);
        auto& line = machine->cpu.data_cache[(data >> 4U) & 511U];
        line.valid = false;
        CHECK(!compiled->execute(state));
        line.valid = true;
        registers[1] = 0xffffffffa0003000ULL;
        CHECK(!compiled->execute(state));
        if (opcode != 0x20U && opcode != 0x24U) {
            registers[1] = data + 1U;
            CHECK(!compiled->execute(state));
        }
    }
}

TEST(cpu_native_load_blocks_match_internal_and_final_load_interlocks) {
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native, interlock_program);
    prepare(ordinary, interlock_program);
    warm(native, ordinary);
    CHECK_EQ(native.cpu.gpr[2], 0xffffffff807fff01ULL);
    CHECK_EQ(native.cpu.gpr[4], 0x12U);
    CHECK_EQ(native.cpu.gpr[8], 0xfedcba98U);
    CHECK_EQ(native.cpu.gpr[9], 0xfffffffffedcba98ULL);
}

TEST(cpu_native_load_sequences_keep_updated_cached_registers_and_address_aliases) {
    const std::array program{
        immediate(0x19U, 20, 20, 1), immediate(0x24U, 20, 2, 0),   immediate(0x19U, 20, 20, 1),
        immediate(0x25U, 20, 20, 0), immediate(0x0bU, 20, 20, -1), immediate(0x0dU, 20, 3, 0x1234),
        special(0x2dU, 20, 3, 20),
    };
    const auto compiled = CpuNativeCode::compile(program);
    CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
    if (!compiled)
        return;
    auto machine = std::make_unique<System>();
    prepare(*machine, program);
    auto registers = machine->cpu.gpr;
    for (const u32 word : program)
        machine->cpu.execute(word);
    CpuNativeState state{registers.data(), machine->cpu.data_cache.data()};
    CHECK(compiled->execute(state));
    CHECK(registers == machine->cpu.gpr);
    CHECK_EQ(registers[2], 0x7fU);
    CHECK_EQ(registers[3], 0x1235U);
    CHECK_EQ(registers[20], 0x1236U);
}

TEST(cpu_native_load_blocks_restore_gprs_before_cache_guard_fallback) {
    constexpr std::array<u32, 5> guard_program{
        immediate(0x09, 10, 10, 1), // ADDIU t2,t2,1.
        immediate(0x09, 11, 11, 1), // ADDIU t3,t3,1.
        immediate(0x23, 20, 2, 0),  // LW v0,0(s4).
        0x08000400U,                // J code.
        0U,
    };
    auto systems = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *systems;
    prepare(native, guard_program);
    prepare(ordinary, guard_program);
    warm(native, ordinary);
    const u64 compiled = native.cpu.native_block_instructions();
    for (auto* system : {&native, &ordinary}) {
        system->cpu.set_pc(code);
        system->cpu.gpr[10] = 100;
        system->cpu.gpr[11] = 200;
        system->cpu.data_cache[(data >> 4U) & 511U].valid = false;
    }
    compare(native, ordinary, 3);
    CHECK_EQ(native.cpu.gpr[10], 101U);
    CHECK_EQ(native.cpu.gpr[11], 201U);
    CHECK_EQ(native.cpu.gpr[2], 0xffffffff807fff01ULL);
    CHECK_EQ(native.cpu.native_block_instructions(), compiled);
}

TEST(cpu_native_load_blocks_rollback_cached_reads_at_rsp_interrupt_boundaries) {
    for (unsigned local_prefix = 0; local_prefix < 3U; ++local_prefix) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        prepare(native, interlock_program);
        prepare(ordinary, interlock_program);
        warm(native, ordinary);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.set_pc(code);
            system->cpu.write_cop0(12, 0x34000401U);
            system->bus.write(0x0430000cU, 4, 2);
            for (unsigned index = 0; index < local_prefix; ++index)
                system->bus.write(0x04001000U + index * 4U, 4, 0x24210001U);
            system->bus.write(0x04001000U + local_prefix * 4U, 4, 0x0000000dU);
            system->rsp.write_register(0x10, 0x101U);
        }
        compare(native, ordinary, CpuNativeCode::maximum_instructions);
        CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
    }
}
