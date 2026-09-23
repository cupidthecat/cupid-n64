#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <bit>
#include <initializer_list>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u32 divide = (0x11U << 26U) | (16U << 21U) | (4U << 16U) | (2U << 11U) | (6U << 6U) | 3U;

void prepare(System& system, std::initializer_list<u32> program, unsigned phase) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000401U);
    system.bus.write(0x0430000cU, 4, 2U); // Enable the SP source at MI and IP2 at the CPU.
    unsigned offset = 0;
    for (const u32 word : program) {
        system.bus.write(0x1000U + offset, 4, word);
        offset += 4;
    }
    auto& line = system.cpu.instruction_cache[(code >> 5U) & 511U];
    line = {};
    line.valid = true;
    line.tag = 0x1000U;
    for (unsigned byte = 0; byte < line.data.size(); ++byte)
        line.data[byte] = system.bus.rdram[0x1000U + byte];
    system.cpu.fpu.control = 1U << 24U;
    system.cpu.fpu.registers[2] = std::bit_cast<u32>(3.5f);
    system.cpu.fpu.registers[4] = std::bit_cast<u32>(1.75f);
    system.cpu.fpu.registers[6] = 0x12345678U;
    system.cpu.set_pc(code);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
    system.advance(phase);
    system.settle();
}

void hot_loop(System& system, unsigned phase) {
    prepare(system, {0U, 0x25080001U, 0x35290001U, 0x1000fffdU, 0U}, phase);
}

void start_rsp(System& system, std::initializer_list<u32> program) {
    unsigned offset = 0x1000U;
    for (const u32 word : program) {
        write_be32(system.rsp.memory.data() + offset, word);
        offset += 4U;
    }
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 1U);
}

void equivalent(System& first, System& second) {
    first.settle();
    second.settle();
    CHECK_EQ(first.cpu.cycles, second.cpu.cycles);
    CHECK_EQ(first.cpu.instruction_count, second.cpu.instruction_count);
    CHECK_EQ(first.cpu.pc, second.cpu.pc);
    CHECK_EQ(first.cpu.next_pc, second.cpu.next_pc);
    CHECK_EQ(first.cpu.hi, second.cpu.hi);
    CHECK_EQ(first.cpu.lo, second.cpu.lo);
    CHECK_EQ(first.cpu.exception_pending, second.cpu.exception_pending);
    CHECK_EQ(first.cpu.frozen, second.cpu.frozen);
    CHECK(first.cpu.gpr == second.cpu.gpr);
    CHECK(first.cpu.cp0 == second.cpu.cp0);
    CHECK(first.cpu.fpu.registers == second.cpu.fpu.registers);
    CHECK_EQ(first.cpu.fpu.control, second.cpu.fpu.control);
    CHECK_EQ(first.cpu.read_cop0(1), second.cpu.read_cop0(1));
    CHECK_EQ(first.bus.output_clock(), second.bus.output_clock());
    CHECK_EQ(first.bus.interrupt_pending(), second.bus.interrupt_pending());
    CHECK(first.bus.rdram == second.bus.rdram);
    CHECK(first.rsp.memory == second.rsp.memory);
    CHECK_EQ(first.rsp.pc, second.rsp.pc);
    for (unsigned offset = 0; offset < 0x1cU; offset += 4U)
        CHECK_EQ(first.rsp.read_register(offset), second.rsp.read_register(offset));
    for (unsigned index = 0; index < first.cpu.data_cache.size(); ++index) {
        const auto& left = first.cpu.data_cache[index];
        const auto& right = second.cpu.data_cache[index];
        CHECK(left.data == right.data);
        CHECK_EQ(left.tag, right.tag);
        CHECK_EQ(left.valid, right.valid);
        CHECK_EQ(left.dirty, right.dirty);
    }
}

void compare_slice(System& batched, System& stepped, unsigned steps, u64 budget = 10000U) {
    const u64 start = stepped.cpu.cycles;
    unsigned used = 0;
    while (used < steps && stepped.cpu.cycles - start < budget && !stepped.cpu.frozen) {
        stepped.cpu.step();
        ++used;
    }
    CHECK_EQ(batched.cpu.run_slice(steps, budget), used);
    equivalent(batched, stepped);
}

void continue_steps(System& first, System& second) {
    for (unsigned index = 0; index < 32; ++index) {
        first.cpu.step();
        second.cpu.step();
    }
    equivalent(first, second);
}
} // namespace

TEST(cpu_cached_coupled_rsp_ignores_irq_cleared_before_divide_retirement) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, divide, 0x34080055U, 0x340900aaU, 0x400a4800U}, phase);
            start_rsp(*system, {0x24010010U, 0x24020008U, // Values for setting and clearing SP IRQ.
                                0x40812000U, 0x40822000U, // Both writes fall inside DIV.S.
                                0x0000000dU});
        }
        const u64 cycles = batched.cpu.cycles;
        const u64 count = batched.cpu.instruction_count;
        compare_slice(batched, stepped, 3);
        CHECK_EQ(batched.cpu.cycles - cycles, 31U);
        CHECK_EQ(batched.cpu.instruction_count - count, 3U);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 3U);
        CHECK_EQ(batched.cpu.gpr[8], 0x55U);
        CHECK_EQ(batched.cpu.gpr[9], 0xaaU);
        CHECK_EQ(batched.cpu.fpu.registers[6], std::bit_cast<u32>(2.0f));
        CHECK_EQ(batched.cpu.status() & 2U, 0U);
        CHECK(!batched.bus.interrupt_pending());
        continue_steps(batched, stepped);
    }
}

TEST(cpu_cached_coupled_rsp_accepts_persistent_irq_before_younger_cpu_instruction) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, divide, 0x34080055U, 0x400a4800U}, phase);
            start_rsp(*system, {0x24010010U, 0x40812000U, 0x0000000dU});
        }
        const u64 cycles = batched.cpu.cycles;
        const u64 count = batched.cpu.instruction_count;
        compare_slice(batched, stepped, 2);
        CHECK_EQ(batched.cpu.cycles - cycles, 34U);
        CHECK_EQ(batched.cpu.instruction_count - count, 1U);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 1U);
        CHECK_EQ(batched.cpu.gpr[8], 0U);
        CHECK_EQ(batched.cpu.fpu.registers[6], std::bit_cast<u32>(2.0f));
        CHECK_EQ(batched.cpu.cp0[14], code + 8U);
        CHECK_EQ(batched.cpu.cp0[13] & 0x8000007cU, 0U);
        CHECK((batched.cpu.cp0[13] & 0x400U) != 0U);
        CHECK_EQ(batched.cpu.pc, 0xffffffff80000180ULL);
        continue_steps(batched, stepped);
    }
}

TEST(cpu_cached_coupled_rsp_interrupt_in_branch_preserves_delay_slot_exception_state) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, 0x10000003U, divide, 0U, 0U, 0x34080055U, 0x400a4800U}, phase);
            start_rsp(*system, {0x24010010U, 0x40812000U, 0x0000000dU});
            system->rsp.tick(1); // Seed the status-write value without advancing CPU/RCP phase.
            CHECK_EQ(system->rsp.pc, 4U);
        }
        const bool before_delay_slot = phase != 2U;
        const u64 cycles = batched.cpu.cycles;
        compare_slice(batched, stepped, before_delay_slot ? 2U : 3U);
        CHECK_EQ(batched.cpu.cycles - cycles, before_delay_slot ? 6U : 35U);
        CHECK_EQ(batched.cpu.cp0[14], before_delay_slot ? code + 4U : code + 20U);
        CHECK_EQ(batched.cpu.cp0[13] & 0x80000000U, before_delay_slot ? 0x80000000U : 0U);
        CHECK_EQ(batched.cpu.fpu.registers[6], before_delay_slot ? 0x12345678U : std::bit_cast<u32>(2.0f));
        CHECK_EQ(batched.cpu.gpr[8], 0U);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), before_delay_slot ? 1U : 2U);
        continue_steps(batched, stepped);
    }
}

TEST(cpu_cached_coupled_rsp_dma_started_inside_slice_transfers_before_exact_row_issue) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const bool to_sp : {false, true}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                hot_loop(*system, phase);
                for (unsigned word = 0; word < 8; ++word) {
                    system->bus.write(0x3000U + word * 4U, 4, 0x55667700U + word);
                    write_be32(system->rsp.memory.data() + 0x200U + word * 4U, 0x11223300U + word);
                }
                start_rsp(*system,
                          {0x24010200U, 0x24023000U, 0x2403001fU, // Descriptor values at C1-C3.
                           0x40810000U, 0U, 0U, // SP address at C4; leave the COP0 store interlock clear.
                           0x40820800U, 0U, 0U, // RDRAM address at C7.
                           to_sp ? 0x40831000U : 0x40831800U,     // Start a 32-byte row at C10.
                           0x40053000U, 0x8c040200U,              // Busy=1 at C11; old DMEM value at C12.
                           0xac040100U, 0x8c060200U, 0xac060104U, // Store old value, then read new value.
                           0x40073000U, 0xac070108U, 0xac05010cU, // Busy after/before transfer.
                           0x0000000dU});
            }
            const u64 clock = batched.bus.output_clock();
            compare_slice(batched, stepped, 96);
            CHECK(batched.cpu.batched_cached_instructions() > 64U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x100U), 0x11223300U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x104U), to_sp ? 0x55667700U : 0x11223300U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x108U), 0U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x10cU), 1U);
            CHECK_EQ(batched.bus.memory.bank_access_clock(0x3000U), clock + 14U);
            for (unsigned word = 0; word < 8; ++word) {
                CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x200U + word * 4U),
                         (to_sp ? 0x55667700U : 0x11223300U) + word);
                CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x3000U + word * 4U),
                         (to_sp ? 0x55667700U : 0x11223300U) + word);
            }
            continue_steps(batched, stepped);
        }
    }
}
