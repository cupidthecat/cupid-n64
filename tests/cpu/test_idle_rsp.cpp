#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;

void write16(u8* target, u16 value) {
    target[0] = static_cast<u8>(value >> 8);
    target[1] = static_cast<u8>(value);
}

u16 read16(const u8* source) {
    return static_cast<u16>((static_cast<u16>(source[0]) << 8) | source[1]);
}

void prepare_idle(System& system, u64 address = code) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
    system.bus.write(physical, 4, 0x1000ffffU); // BEQ zero,zero,self.
    system.bus.write(physical + 4, 4, 0U);
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

void compare_slice(System& batched, System& stepped, unsigned maximum_steps, u64 maximum_cycles = 1000000) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    equivalent(batched, stepped);
}

void rsp_program(System& system, std::initializer_list<u32> instructions) {
    unsigned offset = 0x1000;
    for (const u32 instruction : instructions) {
        write_be32(system.rsp.memory.data() + offset, instruction);
        offset += 4;
    }
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 1U); // Clear halt.
}

void local_rsp_loop(System& system) {
    for (unsigned lane = 0; lane < 8; ++lane)
        write16(system.rsp.memory.data() + 0x100 + lane * 2, static_cast<u16>(lane + 1));
    rsp_program(system, {
                            0x24010100U, // ADDIU at,zero,0x100
                            0xc8212000U, // LQV v1,0(at)
                            0x8c220040U, // LW v0,0x40(at)
                            0x24420001U, // ADDIU v0,v0,1
                            0xac220040U, // SW v0,0x40(at)
                            0x4a010850U, // VADD v1,v1,v1
                            0xe8212001U, // SQV v1,0x10(at)
                            0x1000fffaU, // BEQ zero,zero,0x08
                            0U,
                        });
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

void prepare_halted_branch_wait(System& system) {
    write_be32(system.rsp.memory.data(), 0xfeedfaceU);
    write_be32(system.rsp.memory.data() + 0x1000, 0x10000005U); // BEQ zero,zero,0x18.
    write_be32(system.rsp.memory.data() + 0x1004, 0x0000000dU); // BREAK in the delay slot.
    write_be32(system.rsp.memory.data() + 0x1018, 0xac000000U); // SW zero,0(zero).
    system.rsp.write_register(0x10, 1U);
    system.rsp.tick(1);
    system.rsp.tick(1);
    CHECK_EQ(system.rsp.pc, 0x18U);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 3U);
    CHECK_EQ(read_be32(system.rsp.memory.data()), 0xfeedfaceU);
}

} // namespace

TEST(cpu_idle_rsp_local_execution_matches_steps_at_each_clock_phase) {
    for (const u64 offset : {0ULL, 28ULL}) {
        for (unsigned phase = 0; phase < 3; ++phase) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_idle(*system, code + offset);
                system->advance(phase);
                local_rsp_loop(*system);
            }
            for (const unsigned budget : {1U, 2U, 3U, 4U, 5U, 17U, 62U, 257U, 4097U}) {
                compare_slice(batched, stepped, budget);
                compare_slice(batched, stepped, 101, budget);
            }
            CHECK(batched.cpu.batched_idle_instructions() > 1000);
            CHECK(read_be32(batched.rsp.memory.data() + 0x140) > 10);
            CHECK_EQ(read16(batched.rsp.memory.data() + 0x110), 0x7fffU);
        }
    }
}

TEST(cpu_idle_rsp_stops_before_cop0_and_break_shared_observations) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const unsigned delay : {0U, 1U, 2U, 3U}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_idle(*system);
                stepped_slice(*system, 32 + phase);
                for (unsigned index = 0; index < delay; ++index)
                    write_be32(system->rsp.memory.data() + 0x1000 + index * 4, 0x24010001U);
                const auto append = [&](unsigned index, u32 instruction) {
                    write_be32(system->rsp.memory.data() + 0x1000 + (delay + index) * 4, instruction);
                };
                append(0, 0x40026000U); // MFC0 v0,DPC_CLOCK.
                append(1, 0xac020080U); // SW v0,0x80(zero).
                append(2, 0x24030100U); // ADDIU v1,zero,0x100.
                append(3, 0x40832000U); // MTC0 v1,SP_STATUS: interrupt on BREAK.
                append(4, 0x4a000037U); // VNOP may pair with the COP0 write.
                append(5, 0x0000000dU); // BREAK.
                system->rsp.write_pc(0);
                system->rsp.write_register(0x10, 1U);
                system->bus.write(0x0430000c, 4, 2U); // Enable SP interrupt.
                system->cpu.write_cop0(12, 0x34000401U);
            }
            compare_slice(batched, stepped, 97);
            CHECK(read_be32(batched.rsp.memory.data() + 0x80) != 0);
            CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
            CHECK_EQ(batched.cpu.cp0[14], code);
        }
    }
}

TEST(cpu_idle_rsp_rejects_a_latched_pair_with_cop0_during_operand_stall) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_idle(*system);
        stepped_slice(*system, 32);
        rsp_program(*system, {
                                 0xc8012000U, // LQV v1,0(zero), creating a vector load hazard.
                                 0x4a010850U, // VADD v1,v1,v1.
                                 0x40026000U, // MFC0 v0,DPC_CLOCK, paired behind the stalled VADD.
                                 0xac020080U, // SW v0,0x80(zero).
                                 0x0000000dU, // BREAK.
                             });
        system->rsp.tick(1); // Retire LQV.
        system->rsp.tick(1); // Latch VADD+MFC0 and stall on the LQV result.
    }
    compare_slice(batched, stepped, 97);
    CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80), read_be32(stepped.rsp.memory.data() + 0x80));
    CHECK(read_be32(batched.rsp.memory.data() + 0x80) != 0);
}

TEST(cpu_idle_rsp_rejects_a_latched_local_pair_after_raw_sp_pc_rewrite) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_idle(*system);
        stepped_slice(*system, 32);
        rsp_program(*system, {
                                 0xc8012000U, // LQV v1,0(zero), creating a vector load hazard.
                                 0x4a010850U, // VADD v1,v1,v1.
                                 0x24420001U, // ADDIU v0,v0,1, paired behind the stalled VADD.
                             });
        write_be32(system->rsp.memory.data() + 0x1040, 0x40026000U); // MFC0 v0,DPC_CLOCK.
        write_be32(system->rsp.memory.data() + 0x1044, 0xac020080U); // SW v0,0x80(zero).
        write_be32(system->rsp.memory.data() + 0x1048, 0x0000000dU); // BREAK.
        system->rsp.tick(1);                                         // Retire LQV.
        system->rsp.tick(1);   // Latch the pure-local VADD+ADDIU pair and stall.
        system->rsp.pc = 0x40; // Host SP_PC rewrite; step() must discard the latched pair first.
    }
    compare_slice(batched, stepped, 97);
    CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80), read_be32(stepped.rsp.memory.data() + 0x80));
    CHECK(read_be32(batched.rsp.memory.data() + 0x80) != 0);
}

TEST(cpu_idle_rsp_falls_back_for_dma_and_single_step) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_idle(*system);
            stepped_slice(*system, 32);
            local_rsp_loop(*system);
            if (mode == 2) {
                system->rsp.write_register(0x10, 0x41U); // Single step and clear halt.
            } else {
                system->bus.write(0x2000, 4, 0x12345678U);
                system->bus.write(0x2010, 4, 0x87654321U);
                system->rsp.write_register(0, 0x200);
                system->rsp.write_register(4, 0x2000);
                system->rsp.write_register(8, 15);
                if (mode == 1) {
                    system->rsp.write_register(0, 0x220);
                    system->rsp.write_register(4, 0x2010);
                    system->rsp.write_register(8, 15);
                }
            }
        }
        for (const unsigned count : {2U, 5U, 17U, 257U})
            compare_slice(batched, stepped, count);
        if (mode == 2)
            CHECK_EQ(batched.rsp.read_register(0x10) & 0x21U, 0x21U);
        else
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x200), 0x12345678U);
    }
}

TEST(cpu_idle_rsp_batches_between_dma_rows_without_crossing_visibility_edges) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const u32 bank : {0U, 0x1000U}) {
            for (const bool to_sp : {false, true}) {
                System batched, stepped;
                for (auto* system : {&batched, &stepped}) {
                    prepare_idle(*system);
                    stepped_slice(*system, 32 + phase);
                    local_rsp_loop(*system);
                    for (unsigned offset = 0; offset < 0x600; offset += 4)
                        system->bus.write(0x4000U + offset, 4, 0x24420001U);
                    system->rsp.write_register(0, bank | 0xff0U);
                    system->rsp.write_register(4, 0x4000);
                    system->rsp.write_register(to_sp ? 8U : 12U, 0x11ffU);
                    system->rsp.write_register(0, bank | 0x200U);
                    system->rsp.write_register(4, 0x4400);
                    system->rsp.write_register(to_sp ? 8U : 12U, 0x1ffU);
                }
                const u64 previous = batched.cpu.batched_idle_instructions();
                compare_slice(batched, stepped, 31);
                CHECK(batched.cpu.batched_idle_instructions() > previous + 16);
                CHECK_EQ(batched.rsp.read_register(0x10) & 0xcU, 0xcU);
                for (const unsigned budget : {1U, 2U, 17U, 43U, 1U, 97U, 257U}) {
                    compare_slice(batched, stepped, budget);
                    for (const u32 offset : {0U, 4U, 8U, 12U, 0x14U, 0x18U})
                        CHECK_EQ(batched.rsp.read_register(offset), stepped.rsp.read_register(offset));
                    CHECK(batched.bus.rdram == stepped.bus.rdram);
                    CHECK_EQ(batched.bus.memory.bank_access_clock(0x4000),
                             stepped.bus.memory.bank_access_clock(0x4000));
                }
                CHECK_EQ(batched.rsp.read_register(0x10) & 0xcU, 0U);
            }
        }
    }
}

TEST(cpu_idle_rsp_dma_register_polling_matches_each_transfer_boundary) {
    constexpr std::array<unsigned, 26> registers{0, 1,  2,  3,  5,  6,  16, 17, 18, 19, 21, 22, 8,
                                                 9, 10, 11, 13, 14, 15, 24, 25, 26, 27, 29, 30, 31};
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const u32 bank : {0U, 0x1000U}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_idle(*system);
                stepped_slice(*system, 32 + phase);
                unsigned address = 0x1000;
                for (unsigned index = 0; index < registers.size(); ++index) {
                    write_be32(system->rsp.memory.data() + address, 0x40020000U | (registers[index] << 11U));
                    write_be32(system->rsp.memory.data() + address + 4U, 0xac020800U | (index * 4U));
                    address += 8;
                }
                write_be32(system->rsp.memory.data() + address, 0x1000ffcbU);
                write_be32(system->rsp.memory.data() + address + 4U, 0U);
                system->rsp.write_pc(0);
                system->rsp.write_register(0x10, 1U);
                for (unsigned offset = 0; offset < 0x600; offset += 4)
                    system->bus.write(0x4000U + offset, 4, 0x241f0042U);
                system->rsp.write_register(0, bank | 0x400U);
                system->rsp.write_register(4, 0x4000);
                system->rsp.write_register(8, 0x11ffU);
                system->rsp.write_register(0, bank | 0xa00U);
                system->rsp.write_register(4, 0x4400);
                system->rsp.write_register(8, 0x1ffU);
            }
            const u64 previous = batched.cpu.batched_idle_instructions();
            compare_slice(batched, stepped, 31);
            CHECK(batched.cpu.batched_idle_instructions() > previous + 16);
            for (unsigned cycle = 0; cycle < 80; ++cycle)
                compare_slice(batched, stepped, 1);
            for (const unsigned budget : {2U, 17U, 43U, 97U, 257U}) {
                compare_slice(batched, stepped, budget);
                CHECK(batched.bus.rdram == stepped.bus.rdram);
            }
            CHECK_EQ(batched.rsp.read_register(0x10) & 0xcU, 0U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x810U), 0U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x814U), 0U);
        }
    }
}

TEST(cpu_idle_rsp_preserves_count_compare_interrupt_boundaries) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const unsigned distance : {1U, 2U, 3U, 17U, 127U}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_idle(*system);
                stepped_slice(*system, 32 + phase);
                local_rsp_loop(*system);
                system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + distance);
                system->cpu.write_cop0(12, 0x34008001U);
            }
            compare_slice(batched, stepped, 300);
            CHECK_EQ(batched.cpu.cp0[14], code);
            CHECK_EQ(batched.cpu.cp0[13] & 0x8000U, 0x8000U);
        }
    }
}

TEST(cpu_idle_rsp_callbacks_observe_materialized_cpu_and_rsp_state) {
    using Observation = std::array<u64, 7>;
    for (unsigned mutation = 0; mutation < 2; ++mutation) {
        System batched, stepped;
        std::vector<Observation> first, second;
        const auto attach = [&](System& system, std::vector<Observation>& observations) {
            prepare_idle(system);
            local_rsp_loop(system);
            short_video(system);
            system.bus.write(0x04500010, 4, 99);
            const auto observe = [&, machine = &system, output = &observations] {
                output->push_back({machine->cpu.cycles, machine->cpu.instruction_count, machine->cpu.pc,
                                   machine->cpu.cp0[9], machine->rsp.pc,
                                   read_be32(machine->rsp.memory.data() + 0x140),
                                   machine->bus.output_clock()});
                if (output->size() != 1)
                    return;
                if (mutation == 0)
                    machine->cpu.request_nmi();
                else
                    machine->cpu.set_pc(code + 0x100);
            };
            system.bus.set_audio_sample_output([observe](const AudioSample&) { observe(); });
            system.bus.set_video_output([observe](VideoField) { observe(); });
        };
        attach(batched, first);
        attach(stepped, second);
        compare_slice(batched, stepped, 4096);
        CHECK(!first.empty());
        CHECK(first == second);
    }
}

TEST(cpu_idle_rsp_register_polling_observes_callback_changes_at_the_same_clock) {
    using Observation = std::array<u64, 5>;
    System batched, stepped;
    std::vector<Observation> first, second;
    const auto attach = [&](System& system, std::vector<Observation>& observations) {
        prepare_idle(system);
        rsp_program(system, {
                                0x40025800U, // MFC0 v0,DPC_STATUS.
                                0xac020080U, // SW v0,0x80(zero).
                                0x1000fffdU, // BEQ zero,zero,0.
                                0U,
                            });
        system.bus.write(0x04500010, 4, 99);
        system.bus.set_audio_sample_output([machine = &system, output = &observations](const AudioSample&) {
            output->push_back({machine->cpu.cycles, machine->rsp.pc,
                               read_be32(machine->rsp.memory.data() + 0x80),
                               machine->bus.rdp.read_register(0x0c), machine->bus.output_clock()});
            machine->bus.rdp.write_register(0x0c, (output->size() & 1U) != 0U ? 8U : 4U);
        });
    };
    attach(batched, first);
    attach(stepped, second);
    compare_slice(batched, stepped, 4096);
    CHECK(first.size() > 8);
    CHECK(first == second);
    for (unsigned index = 2; index < first.size(); ++index)
        CHECK_EQ(first[index][2] & 2U, (index & 1U) != 0U ? 2U : 0U);
    CHECK(batched.cpu.batched_idle_instructions() > 1000);
}

TEST(cpu_idle_rsp_keeps_a_halted_branch_bubble_before_resume) {
    System batched, stepped;
    prepare_idle(batched);
    prepare_idle(stepped);
    compare_slice(batched, stepped, 32);

    prepare_halted_branch_wait(batched);
    prepare_halted_branch_wait(stepped);
    const u64 previously_batched = batched.cpu.batched_idle_instructions();
    compare_slice(batched, stepped, 4096);
    CHECK(batched.cpu.batched_idle_instructions() > previously_batched);

    for (auto* system : {&batched, &stepped}) {
        system->rsp.write_register(0x10, 5U); // Clear halt and broke.
        system->rsp.tick(1);
        CHECK_EQ(read_be32(system->rsp.memory.data()), 0U);
        CHECK_EQ(system->rsp.pc, 0x1cU);
    }
}
