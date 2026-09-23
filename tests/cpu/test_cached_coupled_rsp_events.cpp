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

void prepare_cpu(System& system, std::initializer_list<u32> instructions, u32 status = 0x34000401U) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.cpu.write_cop0(6, 7U);
    write_program(system, instructions);
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system, code);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
    CHECK_EQ(system.cpu.batched_cached_instructions(), 0U);
    system.cpu.write_cop0(12, status);
}

void prepare_loop_cpu(System& system, u32 status = 0x34000401U) {
    prepare_cpu(system,
                {
                    0U,
                    immediate(0x09U, 8, 8, 1),                        // ADDIU t0,t0,1.
                    immediate(0x0eU, 8, 9, 0x55),                     // XORI t1,t0,0x55.
                    (8U << 21U) | (9U << 16U) | (10U << 11U) | 0x21U, // ADDU t2,t0,t1.
                    immediate(0x0cU, 10, 11, 0xff),                   // ANDI t3,t2,0xff.
                    immediate(0x0dU, 11, 12, 0x100),                  // ORI t4,t3,0x100.
                    immediate(0x05U, 8, 0, -6),                       // BNE t0,zero,code+4.
                    immediate(0x0dU, 13, 13, 1),                      // ORI t5,t5,1 (delay slot).
                },
                status);
    system.cpu.gpr[8] = 1U;
}

void rsp_program(System& system, std::initializer_list<u32> instructions, u32 address = 0) {
    u32 offset = 0x1000U + (address & 0x0ffcU);
    for (const u32 instruction : instructions) {
        write_be32(system.rsp.memory.data() + offset, instruction);
        offset += 4U;
    }
}

void start_rsp(System& system, u32 address = 0, u32 status_command = 1U) {
    system.rsp.write_pc(address);
    system.rsp.write_register(0x10U, status_command);
}

void shared_clock_loop(System& system) {
    rsp_program(system, {
                            0x40026000U, // MFC0 v0,DPC_CLOCK.
                            0xac020080U, // SW v0,0x80(zero).
                            0x24430001U, // ADDIU v1,v0,1.
                            0xac030084U, // SW v1,0x84(zero).
                            0x1000fffbU, // BEQ zero,zero,0.
                            0U,
                        });
    start_rsp(system);
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
    CHECK_EQ(batched.cpu.hi, stepped.cpu.hi);
    CHECK_EQ(batched.cpu.lo, stepped.cpu.lo);
    CHECK_EQ(batched.cpu.exception_pending, stepped.cpu.exception_pending);
    CHECK_EQ(batched.cpu.frozen, stepped.cpu.frozen);
    CHECK_EQ(batched.cpu.linked, stepped.cpu.linked);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK_EQ(batched.cpu.read_cop0(1), stepped.cpu.read_cop0(1));
    CHECK_EQ(batched.cpu.read_cop0(9), stepped.cpu.read_cop0(9));
    CHECK(batched.cpu.fpu.registers == stepped.cpu.fpu.registers);
    CHECK_EQ(batched.cpu.fpu.control, stepped.cpu.fpu.control);
    for (unsigned index = 0; index < batched.cpu.instruction_cache.size(); ++index) {
        const auto& first = batched.cpu.instruction_cache[index];
        const auto& second = stepped.cpu.instruction_cache[index];
        CHECK(first.data == second.data);
        CHECK_EQ(first.tag, second.tag);
        CHECK_EQ(first.valid, second.valid);
    }
    for (unsigned index = 0; index < batched.cpu.data_cache.size(); ++index) {
        const auto& first = batched.cpu.data_cache[index];
        const auto& second = stepped.cpu.data_cache[index];
        CHECK(first.data == second.data);
        CHECK_EQ(first.tag, second.tag);
        CHECK_EQ(first.valid, second.valid);
        CHECK_EQ(first.dirty, second.dirty);
    }
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK(batched.rsp.memory == stepped.rsp.memory);
    for (u32 offset = 0; offset <= 0x18U; offset += 4U)
        CHECK_EQ(batched.rsp.read_register(offset), stepped.rsp.read_register(offset));
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.bus.rdram_refresh_wait(), stepped.bus.rdram_refresh_wait());
    CHECK_EQ(batched.bus.rdp.current(), stepped.bus.rdp.current());
    CHECK_EQ(batched.bus.rdp.read_register(0x0cU), stepped.bus.rdp.read_register(0x0cU));
    CHECK_EQ(batched.bus.rdp.read_register(0x10U), stepped.bus.rdp.read_register(0x10U));
    CHECK(batched.bus.rdram == stepped.bus.rdram);
}

unsigned compare_slice(System& batched, System& stepped, unsigned maximum_steps,
                       u64 maximum_cycles = 1'000'000U) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    equivalent(batched, stepped);
    return expected;
}

unsigned step_until_exl(System& system, unsigned limit) {
    for (unsigned steps = 1; steps <= limit; ++steps) {
        system.cpu.step();
        if ((system.cpu.cp0[12] & 2U) != 0U)
            return steps;
    }
    return 0;
}

void short_video(System& system) {
    auto& bus = system.bus;
    bus.write(0x04400000U, 4, 0x303U);
    bus.write(0x04400004U, 4, 0x3000U);
    bus.write(0x04400008U, 4, 16U);
    bus.write(0x0440000cU, 4, 4U);
    bus.write(0x04400018U, 4, 13U);
    bus.write(0x0440001cU, 4, 99U);
    bus.write(0x04400020U, 4, (100U << 16U) | 100U);
    bus.write(0x04400024U, 4, (108U << 16U) | 111U);
    bus.write(0x04400028U, 4, (2U << 16U) | 4U);
    bus.write(0x04400030U, 4, 1024U);
    bus.write(0x04400034U, 4, 1024U);
}

} // namespace

TEST(cpu_cached_coupled_rsp_shared_reads_status_and_semaphore_batch_for_all_rcp_phases) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_loop_cpu(*system);
            system->advance(phase);
            rsp_program(*system, {
                                     0x40026000U, // MFC0 v0,DPC_CLOCK.
                                     0xac020080U, // SW v0,0x80(zero).
                                     0x40032000U, // MFC0 v1,SP_STATUS.
                                     0xac030084U, // SW v1,0x84(zero).
                                     0x40043800U, // MFC0 a0,SP_SEMAPHORE: returns 0 and sets it.
                                     0xac040088U, // SW a0,0x88(zero).
                                     0x40053800U, // MFC0 a1,SP_SEMAPHORE: returns 1.
                                     0xac05008cU, // SW a1,0x8c(zero).
                                     0x0000000dU, // BREAK.
                                 });
            start_rsp(*system);
        }

        constexpr unsigned slice = 26U; // Includes the BREAK after the second semaphore store.
        const u64 clock = batched.bus.output_clock();
        const u64 before = batched.cpu.batched_cached_instructions();
        CHECK_EQ(compare_slice(batched, stepped, slice), slice);
        CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), clock + 1U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x84U) & 3U, 0U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x88U), 0U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x8cU), 1U);
        CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
    }
}

TEST(cpu_cached_coupled_rsp_samples_transient_irq_only_after_full_multicycle_fpu_instruction) {
    const auto cpu_program = {
        0U,
        cop1_format(0x10U, 4, 2, 6, 3), // DIV.S f6,f2,f4: 29 cycles.
        immediate(0x0dU, 0, 8, 0x7777), // ORI t0,zero,0x7777 must remain batchable.
        0x40094800U,                    // MFC0 t1,Count: batch exit.
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_cpu(*system, cpu_program);
        system->cpu.fpu.control = flush_subnormals;
        system->cpu.fpu.registers[2] = std::bit_cast<u32>(7.5f);
        system->cpu.fpu.registers[4] = std::bit_cast<u32>(2.5f);
        system->bus.write(0x0430000cU, 4, 2U); // Enable SP in MI.
        rsp_program(*system, {
                                 0x24010010U, // ADDIU at,zero,SET_SP_IRQ.
                                 0x40812000U, // MTC0 at,SP_STATUS: raise SP IRQ.
                                 0x40026000U, // MFC0 v0,DPC_CLOCK while IRQ is high.
                                 0xac020080U, // SW v0,0x80(zero).
                                 0x24010008U, // ADDIU at,zero,CLEAR_SP_IRQ.
                                 0x40812000U, // MTC0 at,SP_STATUS: lower SP IRQ.
                                 0x0000000dU, // BREAK without interrupt-on-break.
                             });
        start_rsp(*system);
    }

    const u64 before = batched.cpu.batched_cached_instructions();
    CHECK_EQ(compare_slice(batched, stepped, 2U), 2U);
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before, 2U);
    CHECK_EQ(static_cast<u32>(batched.cpu.fpu.registers[6]), std::bit_cast<u32>(3.0f));
    CHECK_EQ(batched.cpu.gpr[8], 0x7777U);
    CHECK_EQ(batched.bus.read(0x04300008U, 4) & 1U, 0U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x400U, 0U);
    CHECK_EQ(batched.cpu.cp0[12] & 2U, 0U);
    CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
}

TEST(cpu_cached_coupled_rsp_persistent_irq_after_branch_preserves_epc_and_bd_before_delay_slot) {
    const auto cpu_program = {
        0U,
        immediate(0x05U, 8, 0, 2),      // BNE t0,zero,code+16.
        immediate(0x0dU, 0, 9, 0x1111), // Delay slot must not execute before interrupt acceptance.
        immediate(0x0dU, 0, 10, 0x2222),
        immediate(0x0dU, 0, 11, 0x3333),
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_cpu(*system, cpu_program);
        system->cpu.gpr[8] = 1U;
        system->cpu.gpr[9] = 0xfeedfacecafebeefULL;
        system->bus.write(0x0430000cU, 4, 2U);       // Enable SP in MI.
        system->rsp.write_register(0x10U, 1U << 8U); // Interrupt on BREAK while still halted.
        rsp_program(*system, {0x0000000dU});         // BREAK on the branch's RCP tick.
        start_rsp(*system);
    }

    const u64 before = batched.cpu.batched_cached_instructions();
    CHECK_EQ(compare_slice(batched, stepped, 2U), 2U); // Branch, then interrupt acceptance before delay slot.
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before, 1U);
    CHECK(batched.cpu.exception_pending);
    CHECK_EQ(batched.cpu.cp0[14], code + 4U);
    CHECK((batched.cpu.cp0[13] & 0x80000000U) != 0U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, 0U);
    CHECK((batched.cpu.cp0[12] & 2U) != 0U);
    CHECK_EQ(batched.cpu.gpr[9], 0xfeedfacecafebeefULL);
    CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
}

TEST(cpu_cached_coupled_rsp_started_dma_transfers_exact_row_before_next_issue_in_both_directions) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (const bool to_sp : {false, true}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_loop_cpu(*system);
                system->advance(phase);
                system->bus.write(0x200U, 4, 0x01020304U);
                system->bus.write(0x204U, 4, 0x05060708U);
                write_be32(system->rsp.memory.data() + 0x100U, 0xa0a1a2a3U);
                write_be32(system->rsp.memory.data() + 0x104U, 0xb0b1b2b3U);
                system->rsp.write_register(0x00U, 0x100U);
                system->rsp.write_register(0x04U, 0x200U);
                if (to_sp) {
                    rsp_program(*system, {
                                             0x24010007U, // ADDIU at,zero,7: one eight-byte row.
                                             0x40811000U, // MTC0 at,SP_RD_LEN: start RDRAM -> DMEM.
                                             0U, // Gives the row deadline its exact next issue cycle.
                                             0x8c020100U, // LW v0,0x100(zero): must see transferred data.
                                             0xac020080U, // SW v0,0x80(zero).
                                             0x0000000dU,
                                         });
                } else {
                    rsp_program(*system,
                                {
                                    0x3c025566U, // LUI v0,0x5566.
                                    0x34427788U, // ORI v0,v0,0x7788.
                                    0x24010007U, // ADDIU at,zero,7: one eight-byte row.
                                    0x40811800U, // MTC0 at,SP_WR_LEN: start DMEM -> RDRAM.
                                    0U,          // Transfer must happen before the following store.
                                    0xac020100U, // SW v0,0x100(zero): modify DMEM after DMA sampled it.
                                    0x0000000dU,
                                });
                }
                start_rsp(*system);
            }

            constexpr unsigned slice = 32U;
            const u64 before = batched.cpu.batched_cached_instructions();
            CHECK_EQ(compare_slice(batched, stepped, slice), slice);
            CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
            if (to_sp) {
                CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x100U), 0x01020304U);
                CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x104U), 0x05060708U);
                CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), 0x01020304U);
            } else {
                CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x200U), 0xa0a1a2a3U);
                CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x204U), 0xb0b1b2b3U);
                CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x100U), 0x55667788U);
            }
            CHECK_EQ(batched.rsp.read_register(0x18U), 0U);
            CHECK(batched.bus.memory.bank_access_clock(0x200U) != 0U);
        }
    }
}

TEST(cpu_cached_coupled_rsp_dp_syncfull_interrupts_at_the_next_cpu_instruction_boundary) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_loop_cpu(*system);
        system->bus.write(0x3000U, 8, 0x2900000000000000ULL); // SyncFull.
        system->bus.write(0x0430000cU, 4, 1U << 11U);         // Enable DP in MI.
        rsp_program(*system, {
                                 0x24013000U, // ADDIU at,zero,0x3000.
                                 0x40814000U, // MTC0 at,DPC_START.
                                 0x24013008U, // ADDIU at,zero,0x3008.
                                 0x40814800U, // MTC0 at,DPC_END: execute SyncFull.
                                 0x0000000dU,
                             });
        start_rsp(*system);
    }

    const unsigned acceptance_steps = step_until_exl(stepped, 32U);
    CHECK(acceptance_steps != 0U);
    const u64 before = batched.cpu.batched_cached_instructions();
    CHECK_EQ(batched.cpu.run_slice(acceptance_steps, 1000U), acceptance_steps);
    equivalent(batched, stepped);
    CHECK(batched.cpu.batched_cached_instructions() > before);
    CHECK(batched.cpu.exception_pending);
    CHECK_EQ(batched.bus.rdp.current(), 0x3008U);
    CHECK((batched.bus.read(0x04300008U, 4) & 0x20U) != 0U);
    CHECK((batched.cpu.cp0[13] & 0x400U) != 0U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, 0U);
    CHECK((batched.cpu.cp0[12] & 2U) != 0U);
}

TEST(cpu_cached_coupled_rsp_single_step_and_raw_pc_rewrite_still_allow_private_cpu_batching) {
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (unsigned mode = 0; mode < 2U; ++mode) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_loop_cpu(*system);
                system->advance(phase);
                if (mode == 0) {
                    rsp_program(*system, {
                                             0x40026000U, // MFC0 v0,DPC_CLOCK.
                                             0xac020080U, // Must not execute after single-step halts.
                                             0x0000000dU,
                                         });
                    start_rsp(*system, 0, 0x41U); // Clear halt and set single-step.
                } else {
                    rsp_program(*system, {
                                             0xc8012000U, // LQV v1,0(zero), creating a vector dependency.
                                             0x4a010850U, // VADD v1,v1,v1.
                                             0x24420001U, // ADDIU v0,v0,1, latched with VADD.
                                         });
                    rsp_program(*system,
                                {
                                    0x40026000U, // MFC0 v0,DPC_CLOCK.
                                    0xac020080U, // SW v0,0x80(zero).
                                    0x0000000dU,
                                },
                                0x40U);
                    start_rsp(*system);
                    system->rsp.tick(1);
                    system->rsp.tick(1);
                    CHECK_EQ(system->rsp.pc, 4U);
                    system->rsp.pc = 0x40U;
                }
            }

            constexpr unsigned slice = 16U;
            const u64 before = batched.cpu.batched_cached_instructions();
            CHECK_EQ(compare_slice(batched, stepped, slice), slice);
            CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
            if (mode == 0) {
                CHECK_EQ(batched.rsp.read_register(0x10U) & 0x21U, 0x21U);
                CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), 0U);
            } else {
                CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
                CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
            }
        }
    }
}

TEST(cpu_cached_coupled_rsp_count_compare_boundary_preempts_multicycle_transaction) {
    const auto cpu_program = {
        0U,
        cop1_format(0x10U, 4, 2, 6, 2), // MUL.S f6,f2,f4: five cycles.
        immediate(0x0dU, 0, 8, 1),      // Younger instruction must wait for timer acceptance.
        0x40094800U,
    };
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_cpu(*system, cpu_program, 0x34008401U); // Timer and RCP masks selected.
        system->cpu.fpu.control = flush_subnormals;
        system->cpu.fpu.registers[2] = std::bit_cast<u32>(2.25f);
        system->cpu.fpu.registers[4] = std::bit_cast<u32>(3.5f);
        system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + 2U);
        shared_clock_loop(*system);
    }

    const u64 before_cycles = batched.cpu.cycles;
    const u64 before_batched = batched.cpu.batched_cached_instructions();
    CHECK_EQ(compare_slice(batched, stepped, 2U, 64U), 2U);
    CHECK_EQ(batched.cpu.cycles - before_cycles, 10U);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), before_batched);
    CHECK((batched.cpu.cp0[13] & 0x8000U) != 0U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, 0U);
    CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
}

TEST(cpu_cached_coupled_rsp_output_callbacks_materialize_count_rsp_and_nmi_at_same_boundary) {
    using Observation = std::array<u64, 11>;
    System batched, stepped;
    std::vector<Observation> first, second;
    unsigned first_audio = 0;
    unsigned second_audio = 0;
    unsigned first_video = 0;
    unsigned second_video = 0;

    const auto attach = [](System& system, std::vector<Observation>& output, unsigned& audio,
                           unsigned& video) {
        prepare_loop_cpu(system);
        shared_clock_loop(system);
        short_video(system);
        system.bus.write(0x04500010U, 4, 99U);
        const auto observe = [machine = &system, observations = &output](u64 kind) {
            observations->push_back({kind, machine->cpu.cycles, machine->cpu.instruction_count,
                                     machine->cpu.pc, machine->cpu.next_pc, machine->cpu.cp0[9],
                                     machine->cpu.cp0[13], machine->rsp.pc,
                                     read_be32(machine->rsp.memory.data() + 0x80U),
                                     machine->bus.rdp.read_register(0x10U), machine->bus.output_clock()});
            if (observations->size() == 1U)
                machine->cpu.request_nmi();
        };
        system.bus.set_audio_sample_output([observe, &audio](const AudioSample&) {
            ++audio;
            observe(1U);
        });
        system.bus.set_video_output([observe, &video](VideoField) {
            ++video;
            observe(2U);
        });
    };

    attach(batched, first, first_audio, first_video);
    attach(stepped, second, second_audio, second_video);
    compare_slice(batched, stepped, 4096U);
    CHECK(!first.empty());
    CHECK(first == second);
    CHECK(first_audio != 0U && first_video != 0U);
    CHECK_EQ(first_audio, second_audio);
    CHECK_EQ(first_video, second_video);
    CHECK(batched.cpu.batched_cached_instructions() != 0U);
}
