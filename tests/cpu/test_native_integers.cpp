#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;

u32 special(unsigned function, unsigned rs, unsigned rt, unsigned rd, unsigned shift) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | (shift << 6U) | function;
}

u32 immediate(unsigned opcode, unsigned rs, unsigned rt, u16 value) {
    return (opcode << 26U) | (rs << 21U) | (rt << 16U) | value;
}

constexpr std::array<u64, 12> operands{0,
                                       1,
                                       31,
                                       32,
                                       63,
                                       64,
                                       0xffffffffU,
                                       0x80000000U,
                                       0xffffffff80000000ULL,
                                       0x8000000000000000ULL,
                                       0x0123456789abcdefULL,
                                       0xffffffffffffffffULL};

void compare_instruction(System& machine, u32 word) {
    CHECK(CpuNativeCode::supports(word));
    const auto compiled = CpuNativeCode::compile(std::span(&word, 1));
    CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
    if (!compiled)
        return;
    for (const u64 first : operands) {
        for (const u64 second : operands) {
            auto& cpu = machine.cpu;
            for (unsigned reg = 1; reg < 32; ++reg)
                cpu.gpr[reg] = 0x8000000101020304ULL * reg;
            cpu.gpr[0] = 0;
            cpu.gpr[1] = first;
            cpu.gpr[2] = second;
            auto native = cpu.gpr;
            cpu.execute(word);
            CHECK(compiled->execute(native));
            CHECK(!cpu.exception_pending);
            CHECK(native == cpu.gpr);
        }
    }
}
} // namespace

TEST(cpu_native_integer_emission_matches_wide_operands_and_register_aliases) {
    System machine;
    machine.cpu.write_cop0(12, 0x34000000U);
    constexpr std::array functions{0x00U, 0x02U, 0x03U, 0x04U, 0x06U, 0x07U, 0x14U, 0x16U, 0x17U,
                                   0x21U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U, 0x2aU, 0x2bU, 0x2dU,
                                   0x2fU, 0x38U, 0x3aU, 0x3bU, 0x3cU, 0x3eU, 0x3fU};
    for (unsigned function : functions)
        for (unsigned destination : {0U, 1U, 2U, 3U})
            for (unsigned shift : {0U, 1U, 17U, 31U})
                compare_instruction(machine, special(function, 1, 2, destination, shift));
}

TEST(cpu_native_integer_immediates_sign_extend_and_preserve_upper_logic_bits) {
    System machine;
    machine.cpu.write_cop0(12, 0x34000000U);
    for (unsigned opcode : {9U, 10U, 11U, 12U, 13U, 14U, 15U, 25U})
        for (unsigned source : {0U, 1U})
            for (unsigned destination : {0U, 1U, 2U})
                for (u16 value : std::array<u16, 5>{0, 1, 0x7fff, 0x8000, 0xffff})
                    compare_instruction(machine, immediate(opcode, source, destination, value));
}

TEST(cpu_native_integer_sequences_observe_dependencies_and_zero_writes) {
    const std::array instructions{immediate(0x0f, 0, 1, 0x8000), immediate(9, 1, 1, 0xffff),
                                  special(0x21, 1, 1, 2, 0),     special(0x03, 0, 2, 2, 17),
                                  special(0x2d, 1, 2, 0, 0),     special(0x27, 0, 0, 3, 0),
                                  special(0x3f, 0, 3, 4, 31)};
    const auto compiled = CpuNativeCode::compile(instructions);
    CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
    if (!compiled)
        return;
    System machine;
    machine.cpu.write_cop0(12, 0x34000000U);
    auto native = machine.cpu.gpr;
    for (const u32 instruction : instructions)
        machine.cpu.execute(instruction);
    CHECK(compiled->execute(native));
    CHECK(native == machine.cpu.gpr);
    CHECK_EQ(native[0], 0U);
    CHECK_EQ(native[1], 0x7fffffffU);
    CHECK_EQ(native[4], 0xffffffffffffffffULL);
}

TEST(cpu_native_integer_compilation_rejects_memory_traps_control_and_oversized_blocks) {
    CHECK(!CpuNativeCode::compile({}));
    const std::array<u32, 8> too_long{};
    CHECK(!CpuNativeCode::compile(too_long));
    for (const u32 rejected : {0xac010000U, 0x88010000U, 0x98010000U, 0xc0010000U, 0xd0010000U, 0x10000000U,
                               0x08000400U, 0x40014800U, 0x44010000U, 0x48010000U, 0x20010001U, 0x0000000cU,
                               0x0000000dU, 0x00221820U, 0x00221818U, 0x00001810U, 0x00200011U, 0x00000008U,
                               0xc849fff7U, 0xd849fff7U, 0xe849fff7U, 0xf849fff7U}) {
        CHECK(!CpuNativeCode::supports(rejected));
        const std::array words{0x24010001U, rejected, 0x24210001U};
        CHECK(!CpuNativeCode::compile(words));
    }
}
