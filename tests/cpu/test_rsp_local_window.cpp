#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <initializer_list>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;

void prepare_idle(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.bus.write(0x1000, 4, 0x1000ffffU); // BEQ zero,zero,self.
    system.bus.write(0x1004, 4, 0U);
    system.cpu.set_pc(code);
    system.cpu.step(); // Branch.
    system.cpu.step(); // Delay slot; leaves the hot idle loop latched at the branch.
    CHECK_EQ(system.cpu.pc, code);
}

unsigned stepped_slice(System& system, unsigned maximum_steps, u64 maximum_cycles = 1'000'000) {
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
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK_EQ(batched.cpu.read_cop0(1), stepped.cpu.read_cop0(1));
    CHECK_EQ(batched.cpu.read_cop0(9), stepped.cpu.read_cop0(9));
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.bus.rdram_refresh_wait(), stepped.bus.rdram_refresh_wait());
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10), stepped.rsp.read_register(0x10));
    CHECK(batched.rsp.memory == stepped.rsp.memory);
}

void compare_slice(System& batched, System& stepped, unsigned maximum_steps, u64 maximum_cycles = 1'000'000) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    batched.settle();
    stepped.settle();
    equivalent(batched, stepped);
}

void rsp_program(System& system, std::initializer_list<u32> instructions, u32 start = 0) {
    unsigned offset = 0x1000;
    for (const u32 instruction : instructions) {
        write_be32(system.rsp.memory.data() + offset, instruction);
        offset += 4;
    }
    system.rsp.write_pc(start);
    system.rsp.write_register(0x10, 1U); // Clear halt.
}

void pair_loop(System& system, bool unaligned) {
    constexpr u32 vnop = 0x4a000037U;
    constexpr u32 increment = 0x24210001U; // ADDIU at,at,1.
    constexpr u32 store = 0xac010080U;     // SW at,0x80(zero).
    if (!unaligned) {
        rsp_program(system, {
                                vnop,
                                increment,
                                store,
                                0x1000fffcU, // BEQ zero,zero,0.
                                0U,
                            });
        return;
    }

    rsp_program(system,
                {
                    0U,
                    vnop,
                    increment,
                    store,
                    0x1000fffcU, // BEQ zero,zero,4.
                    0U,
                },
                4U);
}

} // namespace

TEST(cpu_rsp_local_window_matches_revisited_aligned_and_unaligned_pair_targets) {
    for (const bool unaligned : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_idle(*system);
            pair_loop(*system, unaligned);
        }

        compare_slice(batched, stepped, 2048);
        CHECK(batched.cpu.batched_idle_instructions() > 8U);
        CHECK(read_be32(batched.rsp.memory.data() + 0x80) > 8U);
    }
}

TEST(cpu_rsp_local_window_wraps_raw_pair_from_0xffc_to_zero) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_idle(*system);
        write_be32(system->rsp.memory.data() + 0x1ffc, 0x24210001U); // ADDIU at,at,1.
        write_be32(system->rsp.memory.data() + 0x1000, 0x4a000037U); // VNOP, paired across IMEM wrap.
        write_be32(system->rsp.memory.data() + 0x1004, 0xac010080U); // SW at,0x80(zero).
        write_be32(system->rsp.memory.data() + 0x1008, 0x080003ffU); // J 0x0ffc.
        write_be32(system->rsp.memory.data() + 0x100c, 0U);          // Delay slot.
        system->rsp.write_pc(0x0ffcU);
        system->rsp.write_register(0x10, 1U); // Clear halt.
    }

    compare_slice(batched, stepped, 2048);
    CHECK(batched.cpu.batched_idle_instructions() > 8U);
    CHECK(read_be32(batched.rsp.memory.data() + 0x80) > 8U);
}

TEST(cpu_rsp_local_window_preserves_short_and_long_run_boundaries) {
    for (const u64 maximum_cycles : {11ULL, 12ULL, 13ULL, 32ULL}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_idle(*system);
            pair_loop(*system, false);
        }
        compare_slice(batched, stepped, 64, maximum_cycles);
        if (maximum_cycles >= 12U)
            CHECK(batched.cpu.batched_idle_instructions() != 0U);
    }
}

TEST(cpu_rsp_local_window_does_not_survive_first_or_second_word_host_changes) {
    for (unsigned changed_word = 0; changed_word < 2; ++changed_word) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_idle(*system);
            rsp_program(*system, {
                                     0x24210001U, // ADDIU at,at,1.
                                     0xac010080U, // SW at,0x80(zero).
                                     0x1000fffdU, // BEQ zero,zero,0.
                                     0U,
                                 });
        }
        compare_slice(batched, stepped, 256);
        const u32 before = read_be32(batched.rsp.memory.data() + 0x80);

        for (auto* system : {&batched, &stepped}) {
            if (changed_word == 0) {
                write_be32(system->rsp.memory.data() + 0x1000, 0x24210005U); // ADDIU at,at,5.
            } else {
                write_be32(system->rsp.memory.data() + 0x1004, 0x40026000U); // MFC0 v0,DPC_CLOCK.
                write_be32(system->rsp.memory.data() + 0x1008, 0xac020084U); // SW v0,0x84(zero).
                write_be32(system->rsp.memory.data() + 0x100c, 0x0000000dU); // BREAK.
            }
            system->rsp.write_pc(0);
        }

        compare_slice(batched, stepped, 256);
        if (changed_word == 0) {
            CHECK(read_be32(batched.rsp.memory.data() + 0x80) >= before + 5U);
        } else {
            CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
            CHECK(read_be32(batched.rsp.memory.data() + 0x84) != 0U);
        }
    }
}

TEST(cpu_rsp_local_window_revalidates_words_between_calls_without_pc_redirect) {
    for (unsigned changed_word = 0; changed_word < 2; ++changed_word) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_idle(*system);
            rsp_program(*system, {
                                     0x24210001U, // ADDIU at,at,1.
                                     0xac010080U, // SW at,0x80(zero).
                                     0x1000fffdU, // BEQ zero,zero,0.
                                     0U,
                                 });
        }

        compare_slice(batched, stepped, 256);

        unsigned ticks = 0;
        while (batched.rsp.pc != 0x0cU && ticks < 16U) {
            CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
            batched.rsp.tick(1);
            stepped.rsp.tick(1);
            ++ticks;
        }
        CHECK_EQ(batched.rsp.pc, 0x0cU);
        CHECK_EQ(stepped.rsp.pc, 0x0cU);
        batched.rsp.tick(1);
        stepped.rsp.tick(1);
        CHECK_EQ(batched.rsp.pc, 0U);
        CHECK_EQ(stepped.rsp.pc, 0U);

        const u32 before = read_be32(batched.rsp.memory.data() + 0x80);
        const u64 previously_batched = batched.cpu.batched_idle_instructions();
        for (auto* system : {&batched, &stepped}) {
            if (changed_word == 0)
                write_be32(system->rsp.memory.data() + 0x1000, 0x24210005U); // ADDIU at,at,5.
            else
                write_be32(system->rsp.memory.data() + 0x1004, 0xac010084U); // SW at,0x84(zero).
        }

        compare_slice(batched, stepped, 256);
        CHECK(batched.cpu.batched_idle_instructions() > previously_batched);
        if (changed_word == 0)
            CHECK(read_be32(batched.rsp.memory.data() + 0x80) >= before + 5U);
        else
            CHECK(read_be32(batched.rsp.memory.data() + 0x84) != 0U);
    }
}

TEST(cpu_rsp_local_window_keeps_latched_words_during_operand_stall) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_idle(*system);
        rsp_program(*system, {
                                 0xc8012000U, // LQV v1,0(zero), creates a vector load hazard.
                                 0x4a010850U, // VADD v1,v1,v1.
                                 0x24020055U, // ADDIU v0,zero,0x55 paired behind the stall.
                                 0xac020080U, // SW v0,0x80(zero).
                                 0x0000000dU,
                             });
        system->rsp.tick(1); // Retire LQV.
        system->rsp.tick(1); // Latch VADD+ADDIU and hold on the LQV result.
        CHECK_EQ(system->rsp.pc, 4U);
        write_be32(system->rsp.memory.data() + 0x1004, 0x0000000dU);
        write_be32(system->rsp.memory.data() + 0x1008, 0x40026000U);
    }

    compare_slice(batched, stepped, 256);
    CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80), 0x55U);
    CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
}

TEST(cpu_rsp_local_window_defers_shared_second_word_until_it_issues) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_idle(*system);
        rsp_program(*system, {
                                 0x10000007U, // BEQ zero,zero,0x20.
                                 0U,
                                 0U,
                                 0U,
                                 0U,
                                 0U,
                                 0U,
                                 0U,
                                 0x24210001U, // Target first word is local.
                                 0x40026000U, // DPC_CLOCK cannot pair with the scalar target.
                                 0xac020080U,
                                 0x0000000dU,
                             });
        system->rsp.tick(1); // Retire the branch.
        system->rsp.tick(1); // Retire its delay slot, leaving the target bubble pending.
        CHECK_EQ(system->rsp.pc, 0x20U);
    }

    compare_slice(batched, stepped, 256);
    CHECK(read_be32(batched.rsp.memory.data() + 0x80) != 0U);
    CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
}
