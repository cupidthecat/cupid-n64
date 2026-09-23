#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 data = 0xffffffff80002000ULL;

constexpr u32 special(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned function) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | function;
}

constexpr u32 immediate(unsigned op, unsigned rs, unsigned rt, int value) {
    return (op << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(value);
}

void write_program(System& system, std::initializer_list<u32> instructions, u64 address = code) {
    const u32 base = static_cast<u32>(address) & 0x1fffffffU;
    unsigned offset = 0;
    for (const u32 instruction : instructions) {
        system.bus.write(base + offset, 4, instruction);
        offset += 4;
    }
}

void prepare(System& system, std::initializer_list<u32> instructions, u64 address = code) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    write_program(system, instructions, address);
    system.cpu.set_pc(address);
}

unsigned stepped_slice(System& system, unsigned maximum_steps, u64 maximum_cycles = 1000000) {
    const u64 start = system.cpu.cycles;
    unsigned count = 0;
    while (count < maximum_steps && system.cpu.cycles - start < maximum_cycles && !system.cpu.frozen) {
        system.cpu.step();
        ++count;
    }
    return count;
}

void equivalent(System& batched, System& stepped) {
    CHECK_EQ(batched.cpu.cycles, stepped.cpu.cycles);
    CHECK_EQ(batched.cpu.instruction_count, stepped.cpu.instruction_count);
    CHECK_EQ(batched.cpu.pc, stepped.cpu.pc);
    CHECK_EQ(batched.cpu.next_pc, stepped.cpu.next_pc);
    CHECK_EQ(batched.cpu.hi, stepped.cpu.hi);
    CHECK_EQ(batched.cpu.lo, stepped.cpu.lo);
    CHECK_EQ(batched.cpu.exception_pending, stepped.cpu.exception_pending);
    CHECK_EQ(batched.cpu.frozen, stepped.cpu.frozen);
    CHECK_EQ(batched.cpu.linked, stepped.cpu.linked);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK(batched.cpu.fpu.registers == stepped.cpu.fpu.registers);
    CHECK_EQ(batched.cpu.fpu.control, stepped.cpu.fpu.control);
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK(batched.rsp.memory == stepped.rsp.memory);
    for (unsigned index = 0; index < batched.cpu.data_cache.size(); ++index) {
        const auto& a = batched.cpu.data_cache[index];
        const auto& b = stepped.cpu.data_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
        CHECK_EQ(a.dirty, b.dirty);
    }
    for (unsigned index = 0; index < batched.cpu.instruction_cache.size(); ++index) {
        const auto& a = batched.cpu.instruction_cache[index];
        const auto& b = stepped.cpu.instruction_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
    }
}

void compare_slice(System& batched, System& stepped, unsigned maximum_steps, u64 maximum_cycles = 1000000) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    batched.settle();
    stepped.settle();
    equivalent(batched, stepped);
}

void probe_next_step(System& batched, System& stepped) {
    batched.cpu.step();
    stepped.cpu.step();
    batched.settle();
    stepped.settle();
    equivalent(batched, stepped);
}

void warm_data_cache(System& system) {
    const u32 physical = static_cast<u32>(data) & 0x1fffffffU;
    auto& line = system.cpu.data_cache[(data >> 4) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    write_be32(line.data.data(), 0x11223344U);
    write_be32(line.data.data() + 4, 0x55667788U);
    write_be32(line.data.data() + 8, 0x99aabbccU);
    write_be32(line.data.data() + 12, 0xddeeff00U);
}

void warm_instruction_cache_line(System& system, u64 address) {
    const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
    const u32 base = physical & ~31U;
    auto& line = system.cpu.instruction_cache[(address >> 5) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    for (unsigned byte = 0; byte < line.data.size(); ++byte)
        line.data[byte] = system.bus.rdram[base + byte];
}

void short_video(System& system) {
    auto& bus = system.bus;
    bus.write(0x04400000, 4, 0x303);
    bus.write(0x04400004, 4, 0x3000);
    bus.write(0x04400008, 4, 16);
    bus.write(0x0440000c, 4, 4);
    bus.write(0x04400018, 4, 13);
    bus.write(0x0440001c, 4, 99);
    bus.write(0x04400020, 4, (100U << 16) | 100U);
    bus.write(0x04400024, 4, (108U << 16) | 111U);
    bus.write(0x04400028, 4, (2U << 16) | 4U);
    bus.write(0x04400030, 4, 1024);
    bus.write(0x04400034, 4, 1024);
}

using Observation = std::array<u64, 4>;

} // namespace

TEST(cpu_cached_private_matches_integer_branch_loops_at_each_system_phase) {
    const auto program = {
        immediate(0x0d, 0, 8, 0),     // ORI t0,zero,0
        immediate(0x09, 8, 8, 1),     // ADDIU t0,t0,1
        immediate(0x0e, 8, 9, 0x55),  // XORI t1,t0,0x55
        special(0, 9, 10, 1, 0x00),   // SLL t2,t1,1
        immediate(0x05, 8, 0, -4),    // BNE t0,zero,code+4
        immediate(0x0d, 11, 11, 1),   // ORI t3,t3,1 (delay slot)
        0x40024800U,                  // MFC0 v0,Count (unsupported exit)
        special(10, 11, 12, 0, 0x21), // ADDU t4,t2,t3
    };
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        prepare(batched, program);
        prepare(stepped, program);
        batched.advance(phase);
        stepped.advance(phase);
        compare_slice(batched, stepped, 257);
        CHECK(batched.cpu.batched_cached_instructions() > 200);
        probe_next_step(batched, stepped);
    }
}

TEST(cpu_cached_private_restores_zero_before_the_next_private_instruction) {
    const auto program = {
        0U,
        immediate(0x09, 0, 0, 1),  // ADDIU zero,zero,1.
        special(0, 0, 2, 0, 0x21), // ADDU v0,zero,zero.
        immediate(0x04, 0, 0, -2), // BEQ zero,zero back to ADDU.
        immediate(0x0d, 0, 3, 7),  // ORI v1,zero,7 (delay slot).
        0x40044800U,               // MFC0 a0,Count (unsupported exit).
    };
    System batched, stepped;
    prepare(batched, program);
    prepare(stepped, program);
    batched.cpu.step();
    stepped.cpu.step();
    compare_slice(batched, stepped, 67);
    CHECK(batched.cpu.batched_cached_instructions() > 50);
    CHECK_EQ(batched.cpu.gpr[0], 0U);
    CHECK_EQ(batched.cpu.gpr[2], 0U);
    CHECK_EQ(batched.cpu.gpr[3], 7U);
    probe_next_step(batched, stepped);
}

TEST(cpu_cached_private_obeys_instruction_cycle_and_compare_edges) {
    const auto program = {
        immediate(0x09, 8, 8, 1),
        immediate(0x0e, 8, 9, 0x1234),
        immediate(0x05, 8, 0, -3),
        special(9, 8, 10, 0, 0x25),
    };
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (unsigned distance = 1; distance < 8; ++distance) {
            System batched, stepped;
            prepare(batched, program);
            prepare(stepped, program);
            batched.advance(phase);
            stepped.advance(phase);
            batched.cpu.step();
            stepped.cpu.step();
            const u32 compare = static_cast<u32>(stepped.cpu.read_cop0(9)) + distance;
            for (auto* system : {&batched, &stepped}) {
                system->cpu.write_cop0(11, compare);
                system->cpu.write_cop0(12, 0x34008001U);
            }
            compare_slice(batched, stepped, 64, 31);
            probe_next_step(batched, stepped);
        }
    }
}

TEST(cpu_cached_private_obeys_small_cycle_budgets) {
    const auto program = {
        immediate(0x09, 8, 8, 1),
        immediate(0x0e, 8, 9, 0x1234),
        immediate(0x05, 8, 0, -3),
        special(9, 8, 10, 0, 0x25),
    };
    for (u64 budget = 0; budget < 17; ++budget) {
        System batched, stepped;
        prepare(batched, program);
        prepare(stepped, program);
        batched.cpu.step();
        stepped.cpu.step();
        compare_slice(batched, stepped, 100, budget);
        probe_next_step(batched, stepped);
    }
}

TEST(cpu_cached_private_preserves_consecutive_load_interlocks) {
    const auto program = {
        0U,
        immediate(0x23, 1, 2, 0),  // LW v0,0(at)
        immediate(0x23, 1, 3, 4),  // LW v1,4(at): independent encoded fields.
        special(3, 2, 4, 0, 0x21), // ADDU a0,v1,v0: depends on the second load.
        0x40054800U,               // MFC0 a1,Count
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, program);
        system->cpu.gpr[1] = data;
        warm_data_cache(*system);
        system->cpu.step(); // Fill/latch the code line and stop on the first load.
    }
    compare_slice(batched, stepped, 2);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
    probe_next_step(batched, stepped);
    CHECK_EQ(batched.cpu.gpr[4], stepped.cpu.gpr[4]);
    CHECK_EQ(batched.cpu.gpr[4], 0x6688aaccU);

    System encoded_batched, encoded_stepped;
    const auto encoded_program = {
        0U,
        immediate(0x23, 1, 2, 0), // LW v0,0(at)
        immediate(0x23, 1, 2, 4), // LW v0,4(at): rt still interlocks on hardware.
        0x40054800U,
    };
    for (auto* system : {&encoded_batched, &encoded_stepped}) {
        prepare(*system, encoded_program);
        system->cpu.gpr[1] = data;
        warm_data_cache(*system);
        system->cpu.step();
    }
    const u64 encoded_start_cycles = encoded_batched.cpu.cycles;
    compare_slice(encoded_batched, encoded_stepped, 2);
    CHECK_EQ(encoded_batched.cpu.batched_cached_instructions(), 2U);
    CHECK_EQ(encoded_batched.cpu.cycles - encoded_start_cycles, 3U);
    probe_next_step(encoded_batched, encoded_stepped);
}

TEST(cpu_cached_private_preserves_cached_store_hits) {
    const auto program = {
        0U,
        immediate(0x2b, 1, 2, 8), // SW v0,8(at).
        immediate(0x23, 1, 3, 8), // LW v1,8(at).
        0x40044800U,              // MFC0 a0,Count.
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, program);
        system->cpu.gpr[1] = data;
        system->cpu.gpr[2] = 0x13579bdfU;
        warm_data_cache(*system);
        system->cpu.step();
    }
    compare_slice(batched, stepped, 2);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
    CHECK_EQ(batched.cpu.gpr[3], 0x13579bdfU);
    const auto& line = batched.cpu.data_cache[(data >> 4) & 511U];
    CHECK(line.dirty);
    CHECK_EQ(read_be32(line.data.data() + 8), 0x13579bdfU);
    probe_next_step(batched, stepped);
}

TEST(cpu_cached_private_leaves_cache_misses_and_faulting_ops_to_step) {
    const auto miss_program = {
        0U,
        immediate(0x23, 1, 2, 0), // LW v0,0(at), with a cold D-cache.
        0x40054800U,              // MFC0 a1,Count.
    };
    System miss_batched, miss_stepped;
    for (auto* system : {&miss_batched, &miss_stepped}) {
        prepare(*system, miss_program);
        system->cpu.gpr[1] = data;
        system->bus.write(static_cast<u32>(data) & 0x1fffffffU, 4, 0x12345678U);
        system->cpu.step();
    }
    compare_slice(miss_batched, miss_stepped, 2);
    CHECK_EQ(miss_batched.cpu.batched_cached_instructions(), 0U);

    const auto overflow_program = {
        0U,
        immediate(0x08, 1, 2, 1), // ADDI v0,at,1 must overflow.
        0U,
    };
    System overflow_batched, overflow_stepped;
    for (auto* system : {&overflow_batched, &overflow_stepped}) {
        prepare(*system, overflow_program);
        system->cpu.gpr[1] = 0x7fffffffU;
        system->cpu.step();
    }
    compare_slice(overflow_batched, overflow_stepped, 2);
    CHECK_EQ(overflow_batched.cpu.batched_cached_instructions(), 0U);
    CHECK_EQ(overflow_batched.cpu.cp0[13] & 0x7cU, static_cast<u32>(Exception::Overflow) << 2);
}

TEST(cpu_cached_private_preserves_unsupported_delay_slot_exceptions_and_pending_nmi) {
    for (bool prefix : {false, true}) {
        System delay_batched, delay_stepped;
        for (auto* system : {&delay_batched, &delay_stepped}) {
            // Taken BEQ followed by SYSCALL in its delay slot.
            if (prefix)
                prepare(*system, {0U, immediate(0x09, 8, 8, 1), immediate(0x04, 0, 0, 1), 0x0000000cU, 0U});
            else
                prepare(*system, {0U, immediate(0x04, 0, 0, 1), 0x0000000cU, 0U});
            system->cpu.step();
        }
        compare_slice(delay_batched, delay_stepped, prefix ? 3U : 2U);
        // A branch alone uses ordinary stepping. With a preceding private
        // instruction it retires in the slice before the faulting delay slot.
        CHECK_EQ(delay_batched.cpu.batched_cached_instructions(), prefix ? 2U : 0U);
        CHECK_EQ(delay_batched.cpu.gpr[8], prefix ? 1U : 0U);
        CHECK_EQ(delay_batched.cpu.cp0[14], code + (prefix ? 8U : 4U));
        CHECK_EQ(delay_batched.cpu.cp0[13] & 0x8000007cU,
                 0x80000000U | (static_cast<u32>(Exception::Syscall) << 2));
    }

    const auto nmi_program = {
        0U,
        immediate(0x09, 8, 8, 1),
        immediate(0x09, 8, 8, 1),
    };
    System nmi_batched, nmi_stepped;
    prepare(nmi_batched, nmi_program);
    prepare(nmi_stepped, nmi_program);
    nmi_batched.cpu.step();
    nmi_stepped.cpu.step();
    nmi_batched.cpu.request_nmi();
    nmi_stepped.cpu.request_nmi();
    compare_slice(nmi_batched, nmi_stepped, 2);
    CHECK_EQ(nmi_batched.cpu.batched_cached_instructions(), 0U);
}

TEST(cpu_cached_private_preserves_fetched_word_and_next_line_miss_timing) {
    const auto mutation_program = {
        0U,
        immediate(0x09, 0, 2, 1), // This word is already latched after the prologue.
        0x40054800U,
    };
    System mutation_batched, mutation_stepped;
    for (auto* system : {&mutation_batched, &mutation_stepped}) {
        prepare(*system, mutation_program);
        system->cpu.step();
        const u32 physical = (static_cast<u32>(code) & 0x1fffffffU) + 4;
        auto& line = system->cpu.instruction_cache[((code + 4) >> 5) & 511U];
        write_be32(line.data.data() + (physical & 28U), immediate(0x09, 0, 2, 2));
    }
    compare_slice(mutation_batched, mutation_stepped, 2);
    CHECK_EQ(mutation_batched.cpu.batched_cached_instructions(), 0U);
    CHECK_EQ(mutation_batched.cpu.gpr[2], 1U);

    System line_batched, line_stepped;
    std::array<u32, 9> line_program{};
    line_program[7] = immediate(0x09, 0, 2, 7);
    line_program[8] = 0x40054800U;
    for (auto* system : {&line_batched, &line_stepped}) {
        test::initialize_memory(*system);
        system->cpu.write_cop0(12, 0x34000000U);
        const u32 base = static_cast<u32>(code) & 0x1fffffffU;
        for (unsigned index = 0; index < static_cast<unsigned>(line_program.size()); ++index)
            system->bus.write(base + index * 4, 4, line_program[index]);
        system->cpu.set_pc(code);
        stepped_slice(*system, 7);
        CHECK_EQ(system->cpu.pc, code + 28);
    }
    compare_slice(line_batched, line_stepped, 2);
    CHECK_EQ(line_batched.cpu.batched_cached_instructions(), 0U);
}

TEST(cpu_cached_private_preserves_device_callbacks_and_local_rsp_execution) {
    const auto program = {
        immediate(0x09, 8, 8, 1),
        immediate(0x0e, 8, 9, 0x4321),
        immediate(0x05, 8, 0, -3),
        special(9, 8, 10, 0, 0x25),
    };
    System batched, stepped;
    std::vector<Observation> a, b;
    const auto attach = [&](System& system, std::vector<Observation>& output) {
        prepare(system, program);
        short_video(system);
        system.bus.set_video_output([&system, &output](VideoField) {
            output.push_back(
                {system.cpu.cycles, system.cpu.instruction_count, system.cpu.pc, system.cpu.read_cop0(9)});
        });
    };
    attach(batched, a);
    attach(stepped, b);
    compare_slice(batched, stepped, 4096);
    CHECK(!a.empty());
    CHECK(a == b);
    CHECK(batched.cpu.batched_cached_instructions() != 0);

    System rsp_batched, rsp_stepped;
    for (auto* system : {&rsp_batched, &rsp_stepped}) {
        prepare(*system, program);
        system->cpu.write_cop0(12, 0x34000401U);
        system->cpu.step();
        system->rsp.write_pc(0);
        system->rsp.write_register(0x10, 1U); // Clear halt; zero IMEM is a local NOP stream.
    }
    compare_slice(rsp_batched, rsp_stepped, 16);
    CHECK_EQ(rsp_batched.cpu.batched_cached_instructions(), 16U);
}

TEST(cpu_cached_line_plan_crosses_guarded_lines_with_direct_integer_ops) {
    const auto program = {
        immediate(0x0d, 0, 8, 1),     // ORI t0,zero,1.
        immediate(0x09, 8, 8, 1),     // ADDIU t0,t0,1.
        immediate(0x0e, 8, 9, 0x55),  // XORI t1,t0,0x55.
        special(0, 9, 10, 2, 0x00),   // SLL t2,t1,2.
        special(10, 8, 11, 0, 0x21),  // ADDU t3,t2,t0.
        special(11, 9, 12, 0, 0x25),  // OR t4,t3,t1.
        special(12, 11, 13, 0, 0x24), // AND t5,t4,t3.
        immediate(0x19, 13, 14, 3),   // DADDIU t6,t5,3.
        special(0, 14, 15, 1, 0x38),  // DSLL t7,t6,1.
        special(0, 15, 15, 1, 0x3a),  // DSRL t7,t7,1.
        special(8, 15, 16, 0, 0x2b),  // SLTU s0,t0,t7.
        immediate(0x05, 0, 0, 1),     // BNE zero,zero,+1 (not taken).
        immediate(0x0d, 0, 17, 9),    // ORI s1,zero,9 (delay slot).
        0x40024800U,                  // MFC0 v0,Count (unsupported exit).
    };
    System batched, stepped;
    prepare(batched, program);
    prepare(stepped, program);
    for (auto* system : {&batched, &stepped}) {
        warm_instruction_cache_line(*system, code);
        warm_instruction_cache_line(*system, code + 32);
    }
    compare_slice(batched, stepped, 13);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 12U);
    CHECK_EQ(batched.cpu.gpr[17], 9U);
    probe_next_step(batched, stepped);
}

TEST(cpu_cached_line_plan_rebuilds_after_live_icache_word_change) {
    const auto program = {
        immediate(0x09, 8, 8, 1),   // ADDIU t0,t0,1.
        immediate(0x0d, 0, 9, 1),   // ORI t1,zero,1; changed after the plan is hot.
        immediate(0x05, 8, 0, -3),  // BNE t0,zero,code.
        immediate(0x0d, 10, 10, 1), // ORI t2,t2,1 (delay slot).
    };
    System batched, stepped;
    prepare(batched, program);
    prepare(stepped, program);
    compare_slice(batched, stepped, 64);
    const u64 previously_batched = batched.cpu.batched_cached_instructions();

    for (auto* system : {&batched, &stepped}) {
        const u64 address = code + 4;
        const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
        auto& line = system->cpu.instruction_cache[(address >> 5) & 511U];
        CHECK(line.valid);
        write_be32(line.data.data() + (physical & 28U), immediate(0x0d, 0, 9, 7));
    }

    compare_slice(batched, stepped, 4);
    CHECK(batched.cpu.batched_cached_instructions() > previously_batched);
    CHECK_EQ(batched.cpu.gpr[9], 7U);
}

TEST(cpu_cached_line_plan_invalidates_transient_latched_decode_before_image_reuse) {
    const u32 word_b = immediate(0x0d, 0, 8, 7);                                   // ORI t0,zero,7.
    const u32 word_a = (2U << 26) | ((static_cast<u32>(code) >> 2) & 0x03ffffffU); // J code.
    const auto program = {
        0U,
        word_b,
        0U,
        0x40024800U, // MFC0 v0,Count (unsupported exit).
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, program);
        warm_instruction_cache_line(*system, code);
    }

    // Build a valid line plan whose slot at code+4 decodes word B.
    compare_slice(batched, stepped, 3);
    const u64 previously_batched = batched.cpu.batched_cached_instructions();
    CHECK(previously_batched != 0);

    for (auto* system : {&batched, &stepped}) {
        system->cpu.set_pc(code);
        system->cpu.gpr[8] = 0;
        const u32 physical = (static_cast<u32>(code) & 0x1fffffffU) + 4;
        auto& line = system->cpu.instruction_cache[((code + 4) >> 5) & 511U];
        CHECK(line.valid);
        write_be32(line.data.data() + (physical & 28U), word_a);
        system->cpu.step(); // The predecessor latches transient word A from code+4.
        write_be32(line.data.data() + (physical & 28U), word_b); // Restore the exact planned image.
    }

    // The first batched attempt must fall back and execute latched A. When execution
    // returns through code, the restored image must re-decode B instead of prefetching
    // stale metadata for A from the formerly valid line plan.
    compare_slice(batched, stepped, 6);
    CHECK(batched.cpu.batched_cached_instructions() > previously_batched);
    CHECK_EQ(batched.cpu.gpr[8], 7U);
}

TEST(cpu_cached_direct_preserves_jump_link_hilo_and_regimm_state) {
    const auto program = {
        0U,
        special(8, 0, 9, 0, 0x09), // JALR t1,t0.
        immediate(0x0d, 0, 10, 1), // ORI t2,zero,1 (delay slot).
        0U,
        0U,
        special(10, 0, 0, 0, 0x11),   // MTHI t2.
        special(0, 0, 11, 0, 0x10),   // MFHI t3.
        special(11, 0, 0, 0, 0x13),   // MTLO t3.
        special(0, 0, 12, 0, 0x12),   // MFLO t4.
        immediate(0x01, 13, 0x10, 1), // BLTZAL t5,+1.
        immediate(0x0d, 0, 15, 0x55), // ORI t7,zero,0x55 (delay slot).
        immediate(0x01, 14, 0x11, 1), // BGEZAL t6,+1.
        immediate(0x0d, 0, 16, 0x66), // ORI s0,zero,0x66 (delay slot).
        0x40024800U,                  // MFC0 v0,Count (unsupported exit).
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, program);
        warm_instruction_cache_line(*system, code);
        warm_instruction_cache_line(*system, code + 32);
        system->cpu.gpr[8] = code + 20;
        system->cpu.gpr[13] = ~0ULL;
        system->cpu.gpr[14] = 1;
    }

    compare_slice(batched, stepped, 11);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 10U);
    CHECK_EQ(batched.cpu.gpr[9], code + 12);
    CHECK_EQ(batched.cpu.gpr[11], 1U);
    CHECK_EQ(batched.cpu.gpr[12], 1U);
    CHECK_EQ(batched.cpu.gpr[15], 0x55U);
    CHECK_EQ(batched.cpu.gpr[16], 0x66U);
    CHECK_EQ(batched.cpu.gpr[31], code + 52);
    probe_next_step(batched, stepped);
}

TEST(cpu_cached_direct_leaves_misaligned_same_line_jump_targets_to_step) {
    for (const bool link : {false, true}) {
        const u32 jump = link ? special(8, 0, 9, 0, 0x09) : special(8, 0, 0, 0, 0x08);
        const auto program = {
            0U,
            jump,
            immediate(0x0d, 0, 10, 0x77), // ORI t2,zero,0x77 (delay slot).
            0U,
        };
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, program);
            system->cpu.gpr[8] = code + 2;
        }

        compare_slice(batched, stepped, 4);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 1U);
        CHECK_EQ(batched.cpu.gpr[10], 0x77U);
        CHECK_EQ(batched.cpu.cp0[8], code + 2);
        CHECK_EQ(batched.cpu.cp0[14], code + 2);
        CHECK_EQ(batched.cpu.cp0[13] & 0x8000007cU, static_cast<u32>(Exception::AddressLoad) << 2);
        if (link)
            CHECK_EQ(batched.cpu.gpr[9], code + 12);
    }
}

TEST(cpu_cached_direct_jalr_uses_prelink_source_when_rd_equals_rs) {
    const auto program = {
        0U,
        special(8, 0, 8, 0, 0x09),    // JALR t0,t0.
        immediate(0x0d, 0, 10, 0x77), // ORI t2,zero,0x77 (delay slot).
        0U,
        0U,
        0x40024800U, // MFC0 v0,Count (unsupported target).
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, program);
        system->cpu.gpr[8] = code + 20;
    }

    compare_slice(batched, stepped, 3);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
    CHECK_EQ(batched.cpu.pc, code + 20);
    CHECK_EQ(batched.cpu.gpr[8], code + 12);
    CHECK_EQ(batched.cpu.gpr[10], 0x77U);
    probe_next_step(batched, stepped);
}

TEST(cpu_cached_direct_regimm_link_aliases_match_existing_ordering) {
    for (const unsigned rt : {0x10U, 0x11U}) {
        const auto program = {
            0U,
            immediate(0x01, 31, rt, 2), // BLTZAL/BGEZAL r31,+2.
            immediate(0x0d, 0, 10, 0x77),
            0U,
            0x40024800U, // MFC0 v0,Count (taken target).
        };
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, program);
            // BLTZAL writes the negative kseg0 link before testing r31; BGEZAL
            // tests this captured positive value before writing the same link.
            system->cpu.gpr[31] = 1;
        }

        compare_slice(batched, stepped, 3);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
        CHECK_EQ(batched.cpu.pc, code + 16);
        CHECK_EQ(batched.cpu.gpr[31], code + 12);
        CHECK_EQ(batched.cpu.gpr[10], 0x77U);
        probe_next_step(batched, stepped);
    }
}

TEST(cpu_cached_memory_preserves_width_signedness_zero_register_and_cache_only_writes) {
    struct Operation {
        unsigned opcode;
        unsigned width;
        bool store;
        bool signed_load;
    };
    constexpr std::array operations{
        Operation{0x20, 1, false, true},  Operation{0x21, 2, false, true},  Operation{0x23, 4, false, true},
        Operation{0x24, 1, false, false}, Operation{0x25, 2, false, false}, Operation{0x27, 4, false, false},
        Operation{0x37, 8, false, false}, Operation{0x28, 1, true, false},  Operation{0x29, 2, true, false},
        Operation{0x2b, 4, true, false},  Operation{0x3f, 8, true, false},
    };
    constexpr u64 stored_value = 0xfedcba9876543210ULL;
    std::array<u8, 16> initial{};
    for (unsigned index = 0; index < initial.size(); ++index)
        initial[index] = static_cast<u8>(0x81U + 7U * index);

    for (const auto& operation : operations) {
        for (unsigned offset = 0; offset < 16; offset += operation.width) {
            for (const unsigned target : {0U, 2U}) {
                System batched, stepped;
                for (auto* system : {&batched, &stepped}) {
                    prepare(*system, {0U, immediate(operation.opcode, 1, target, static_cast<int>(offset)),
                                      immediate(0x0d, 0, 3, 7), 0x40044800U});
                    warm_data_cache(*system);
                    system->cpu.data_cache[(data >> 4) & 511U].data = initial;
                    system->cpu.gpr[1] = data;
                    system->cpu.gpr[2] = stored_value;
                    system->cpu.linked = true;
                    system->cpu.step();
                }
                std::array<u8, 16> ram{};
                for (unsigned index = 0; index < ram.size(); ++index)
                    ram[index] = batched.bus.rdram[0x2000U + index];

                compare_slice(batched, stepped, 2);
                CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
                CHECK_EQ(batched.cpu.gpr[0], 0U);
                CHECK(batched.cpu.linked);
                auto expected = initial;
                if (operation.store) {
                    const u64 value = target == 0 ? 0 : stored_value;
                    for (unsigned byte = 0; byte < operation.width; ++byte)
                        expected[offset + byte] =
                            static_cast<u8>(value >> ((operation.width - byte - 1U) * 8U));
                } else if (target != 0) {
                    u64 value = 0;
                    for (unsigned byte = 0; byte < operation.width; ++byte)
                        value = value * 256U + initial[offset + byte];
                    if (operation.signed_load && (initial[offset] & 0x80U) != 0)
                        value |= ~0ULL << (operation.width * 8U);
                    CHECK_EQ(batched.cpu.gpr[target], value);
                }
                const auto& line = batched.cpu.data_cache[(data >> 4) & 511U];
                CHECK_EQ(line.data, expected);
                CHECK_EQ(line.dirty, operation.store);
                CHECK_EQ(line.tag, 0x2000U);
                for (unsigned index = 0; index < ram.size(); ++index)
                    CHECK_EQ(batched.bus.rdram[0x2000U + index], ram[index]);
                probe_next_step(batched, stepped);
            }
        }
    }
}

TEST(cpu_cached_memory_rechecks_the_address_after_a_load_changes_its_own_base) {
    System batched, stepped;
    constexpr u64 second = data + 16U;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, {0U, immediate(0x37, 1, 1, 0), immediate(0x0d, 0, 9, 7), immediate(0x23, 1, 2, 0),
                          immediate(0x0d, 0, 10, 9), 0x40044800U});
        warm_data_cache(*system);
        auto& first_line = system->cpu.data_cache[(data >> 4) & 511U];
        write_be64(first_line.data.data(), second);
        auto& second_line = system->cpu.data_cache[(second >> 4) & 511U];
        second_line = {};
        second_line.valid = true;
        second_line.tag = 0x2000U;
        write_be32(second_line.data.data(), 0xfedcba98U);
        system->cpu.gpr[1] = data;
        system->cpu.step();
    }
    compare_slice(batched, stepped, 4);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 4U);
    CHECK_EQ(batched.cpu.gpr[1], second);
    CHECK_EQ(batched.cpu.gpr[2], 0xfffffffffedcba98ULL);
    probe_next_step(batched, stepped);
}

TEST(cpu_cached_memory_keeps_alignment_and_external_doubleword_faults_before_cache_access) {
    for (const bool external_doubleword : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system,
                    {0U, immediate(external_doubleword ? 0x37U : 0x2bU, 1, 2, external_doubleword ? 0 : 1),
                     immediate(0x0d, 0, 3, 7), 0x40044800U});
            warm_data_cache(*system);
            system->cpu.gpr[1] = external_doubleword ? 0xffffffff84002000ULL : data;
            system->cpu.gpr[2] = 0x123456789abcdef0ULL;
            if (external_doubleword)
                system->cpu.data_cache[(data >> 4) & 511U].tag = 0x04002000U;
            system->cpu.step();
        }
        const auto bytes = batched.cpu.data_cache[(data >> 4) & 511U].data;
        compare_slice(batched, stepped, 1);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 0U);
        CHECK_EQ(batched.cpu.gpr[2], 0x123456789abcdef0ULL);
        CHECK_EQ(batched.cpu.data_cache[(data >> 4) & 511U].data, bytes);
        CHECK(!batched.cpu.data_cache[(data >> 4) & 511U].dirty);
        CHECK_EQ(batched.cpu.frozen, external_doubleword);
        if (!external_doubleword)
            CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, static_cast<u32>(Exception::AddressStore) << 2U);
    }
}

TEST(cpu_cached_line_plan_rechecks_instruction_edits_at_video_callback_boundaries) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    std::array<std::vector<Observation>, 2> observations;
    for (unsigned index = 0; index < machines->size(); ++index) {
        auto& system = (*machines)[index];
        prepare(system, {immediate(0x09, 8, 8, 1), immediate(0x0d, 0, 9, 1), 0U, 0U, 0U, 0U, 0U, 0U,
                         immediate(0x09, 10, 10, 1), immediate(0x05, 8, 0, -10), 0U});
        warm_instruction_cache_line(system, code);
        warm_instruction_cache_line(system, code + 32);
        short_video(system);
        system.bus.set_video_output([&system, &output = observations[index]](VideoField) {
            output.push_back({system.cpu.cycles, system.cpu.pc, system.cpu.gpr[9], system.cpu.gpr[10]});
            auto& first = system.cpu.instruction_cache[(code >> 5) & 511U];
            auto& second = system.cpu.instruction_cache[((code + 32) >> 5) & 511U];
            write_be32(first.data.data() + 4, immediate(0x0d, 0, 9, 7));
            write_be32(second.data.data(), immediate(0x09, 10, 10, 3));
        });
    }
    compare_slice(batched, stepped, 8192);
    CHECK(!observations[0].empty());
    CHECK(observations[0] == observations[1]);
    CHECK_EQ(batched.cpu.gpr[9], 7U);
    CHECK(batched.cpu.batched_cached_instructions() != 0);
}

TEST(cpu_cached_line_plan_keeps_stale_icache_during_rsp_dma_to_code) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system,
                {immediate(0x09, 8, 8, 1), immediate(0x0e, 8, 9, 0x55), immediate(0x05, 8, 0, -3), 0U});
        warm_instruction_cache_line(*system, code);
        system->cpu.write_cop0(12, 0x34000401U);
        system->cpu.step();
        constexpr std::array<u32, 6> rsp_program{
            0x24010000U, // ADDIU at,zero,0.
            0x40810000U, // MTC0 at,SP_MEM_ADDR.
            0x24011000U, // ADDIU at,zero,0x1000.
            0x40810800U, // MTC0 at,SP_DRAM_ADDR.
            0x2401001fU, // ADDIU at,zero,31.
            0x40811800U, // MTC0 at,SP_WR_LEN; four beats replace the backing code with zeroes.
        };
        for (unsigned index = 0; index < rsp_program.size(); ++index)
            system->bus.write(0x04001000U + index * 4U, 4, rsp_program[index]);
        system->rsp.write_register(0x10, 1); // Start the DMA from inside the coupled CPU slice.
    }
    compare_slice(batched, stepped, 128);
    CHECK_EQ(batched.bus.memory.read(0x1000, 8), 0U);
    CHECK(batched.cpu.gpr[8] > 1U);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 128U);
}
