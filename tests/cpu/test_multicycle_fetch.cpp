#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u32 ddiv = 0x0043001eU;

void prepare(System& system, u32 instruction, bool warm_next, u64 offset = 28) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.cpu.set_pc(code + offset);
    system.bus.write(static_cast<u32>(0x1000 + offset), 4, instruction);
    system.bus.write(0x1020, 4, 0x00002012U); // MFLO a0
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code + offset, 4, ignored, true));
    if (warm_next)
        CHECK(system.cpu.read_memory(code + 32, 4, ignored, true));
}

void operands(System& system) {
    system.cpu.gpr[2] = 0x100000000ULL;
    system.cpu.gpr[3] = 3;
}

} // namespace

TEST(cpu_integer_multicycle_fetch_interlocks_resolve_concurrently) {
    struct Case {
        u32 function;
        u64 left;
        u64 right;
        u64 low;
        u64 high;
        u64 warm_cycles;
    };
    constexpr std::array cases{
        Case{0x18, 0xfffffffffffffffdULL, 7, 0xffffffffffffffebULL, ~0ULL, 5},
        Case{0x19, 0x12345678, 2, 0x2468acf0, 0, 5},
        Case{0x1a, 27, 5, 5, 2, 37},
        Case{0x1b, 27, 5, 5, 2, 37},
        Case{0x1c, 0xfffffffffffffffdULL, 7, 0xffffffffffffffebULL, ~0ULL, 8},
        Case{0x1d, 0x100000000ULL, 2, 0x200000000ULL, 0, 8},
        Case{0x1e, 0x100000000ULL, 3, 0x55555555, 1, 69},
        Case{0x1f, 0x100000000ULL, 3, 0x55555555, 1, 69},
    };
    constexpr std::array<u64, 3> phase_preamble{0, 2, 1};
    constexpr std::array<u64, 3> cold_short_cycles{50, 49, 49};
    for (const auto& item : cases) {
        for (unsigned phase = 0; phase < phase_preamble.size(); ++phase) {
            for (const bool warm_next : {false, true}) {
                System system;
                prepare(system, 0x00430000U | item.function, warm_next);
                system.cpu.gpr[2] = item.left;
                system.cpu.gpr[3] = item.right;
                system.advance(phase_preamble[phase]);
                system.cpu.step();
                CHECK_EQ(system.cpu.lo, item.low);
                CHECK_EQ(system.cpu.hi, item.high);
                const u64 expected =
                    warm_next || item.warm_cycles == 69 ? item.warm_cycles : cold_short_cycles[phase];
                CHECK_EQ(system.cpu.cycles, expected);
                CHECK_EQ(system.cpu.pc, code + 32);
                CHECK_EQ(system.cpu.instruction_count, 1U);
                CHECK(!system.cpu.exception_pending);
            }
        }
    }
}

TEST(cpu_integer_multicycle_fetch_does_not_execute_the_following_instruction) {
    System system;
    prepare(system, ddiv, false);
    operands(system);
    system.cpu.gpr[4] = 0x1234;
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 69U);
    CHECK_EQ(system.cpu.gpr[4], 0x1234U);
    CHECK_EQ(system.cpu.read_cop0(9), 34U);
    CHECK_EQ(system.bus.read(0x04100010, 4), 46U);

    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 70U);
    CHECK_EQ(system.cpu.gpr[4], 0x55555555U);
    CHECK_EQ(system.cpu.read_cop0(9), 35U);
}

TEST(cpu_integer_multicycle_fetch_keeps_compare_pending_until_the_next_instruction) {
    for (const bool warm_next : {false, true}) {
        System system;
        prepare(system, ddiv, warm_next);
        operands(system);
        system.cpu.write_cop0(12, 0x34008001U);
        system.cpu.write_cop0(11, 16);
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, 69U);
        CHECK_EQ(system.cpu.lo, 0x55555555U);
        CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0x8000U);
        CHECK(!system.cpu.exception_pending);

        system.cpu.step();
        CHECK(system.cpu.exception_pending);
        CHECK_EQ(system.cpu.cp0[14], code + 32);
        CHECK_EQ(system.cpu.cp0[13] & 0x80000000U, 0U);
        CHECK_EQ(system.cpu.cycles, 74U);
    }
}

TEST(cpu_integer_multicycle_fetch_in_a_delay_slot_refills_the_branch_target) {
    System system;
    prepare(system, 0x10000019U, false, 24); // BEQ zero, zero, 0x1080
    system.bus.write(0x101c, 4, ddiv);
    system.bus.write(0x1080, 4, 0x00002012U); // MFLO a0
    system.cpu.cache_operation(0x00, code);
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(code + 24, 4, ignored, true));
    operands(system);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 28);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 128);
    CHECK_EQ(system.cpu.cycles, 70U);
    CHECK_EQ(system.cpu.gpr[4], 0U);
    system.cpu.step();
    CHECK_EQ(system.cpu.gpr[4], 0x55555555U);
    CHECK_EQ(system.cpu.cycles, 71U);
}

TEST(cpu_integer_multicycle_fetch_waits_when_refresh_extends_the_refill) {
    struct Case {
        u32 recovery;
        u64 cycles;
    };
    for (const auto& item : {Case{10, 69}, Case{30, 95}}) {
        System system;
        prepare(system, ddiv, false);
        operands(system);
        constexpr u32 hsync = 99;
        system.bus.write(0x0440001c, 4, hsync);
        system.bus.write(0x04700010, 4, 0x20000U | (item.recovery << 8U) | item.recovery);
        system.bus.write(0x0470001c, 4, 0);
        const u64 line =
            ((hsync + 1ULL) * 62500000U + system.video_frequency() - 1U) / system.video_frequency();
        system.bus.tick(line - 33U);
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, item.cycles);
        CHECK_EQ(system.cpu.lo, 0x55555555U);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
        CHECK_EQ(system.cpu.pc, code + 32);
    }
}

TEST(cpu_integer_multicycle_fetch_preserves_standalone_helper_behavior) {
    System system;
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    operands(system);
    system.cpu.execute(ddiv);
    CHECK_EQ(system.cpu.lo, 0x55555555U);
    CHECK_EQ(system.cpu.hi, 1U);
    CHECK_EQ(system.cpu.cycles, 0U);
    system.reset();
    prepare(system, 0, true);
    system.cpu.step();
    CHECK_EQ(system.cpu.cycles, 1U);
}
