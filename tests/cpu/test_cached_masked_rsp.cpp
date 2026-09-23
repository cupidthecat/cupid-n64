#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 cached_dma = 0xffffffff80000200ULL;

constexpr u32 special(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned function) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | (sa << 6U) | function;
}

constexpr u32 immediate(unsigned op, unsigned rs, unsigned rt, int value) {
    return (op << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

constexpr u32 mtc0(unsigned rt, unsigned rd) {
    return (0x10U << 26U) | (4U << 21U) | (rt << 16U) | (rd << 11U);
}

void write_program(System& system, std::initializer_list<u32> instructions) {
    const u32 base = static_cast<u32>(code) & 0x1fffffffU;
    unsigned offset = 0;
    for (const u32 instruction : instructions) {
        system.bus.write(base + offset, 4, instruction);
        offset += 4;
    }
}

void warm_instruction_cache_line(System& system) {
    const u32 physical = static_cast<u32>(code) & 0x1fffffffU;
    const u32 base = physical & ~31U;
    auto& line = system.cpu.instruction_cache[(code >> 5U) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    for (unsigned byte = 0; byte < line.data.size(); ++byte)
        line.data[byte] = system.bus.rdram[base + byte];
}

void warm_dma_cache_line(System& system) {
    const u32 physical = static_cast<u32>(cached_dma) & 0x1fffffffU;
    auto& line = system.cpu.data_cache[(cached_dma >> 4U) & 511U];
    line = {};
    line.valid = true;
    line.dirty = true;
    line.tag = physical & 0xfffff000U;
    line.data.fill(0x5aU);
    write_be32(line.data.data(), 0x11223344U);
    write_be32(line.data.data() + 4, 0x55667788U);
}

void prepare_cpu(System& system, u32 status) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.cpu.write_cop0(6, 7U);
    write_program(system, {
                              0U,                             // Prologue that latches the hot line.
                              immediate(0x09, 8, 8, 1),       // ADDIU t0,t0,1.
                              immediate(0x0e, 8, 9, 0x55),    // XORI t1,t0,0x55.
                              special(8, 9, 10, 0, 0x21),     // ADDU t2,t0,t1.
                              immediate(0x0c, 10, 11, 0xff),  // ANDI t3,t2,0xff.
                              immediate(0x0d, 11, 12, 0x100), // ORI t4,t3,0x100.
                              immediate(0x05, 8, 0, -6),      // BNE t0,zero,code+4.
                              immediate(0x19, 13, 13, 1),     // DADDIU t5,t5,1 (delay slot).
                          });
    system.cpu.gpr[8] = 1U;
    system.cpu.fpu.control = (1U << 24U) | 2U;
    system.cpu.fpu.registers[2] = 0x3ff4000000000000ULL;
    system.cpu.fpu.registers[3] = 0x4004000000000000ULL;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system);
    warm_dma_cache_line(system);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
    CHECK_EQ(system.cpu.batched_cached_instructions(), 0U);
    system.cpu.write_cop0(12, status);
}

void prepare_status_reenable_cpu(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.cpu.write_cop0(6, 7U);
    write_program(system, {
                              0U,
                              immediate(0x09, 8, 8, 1),       // ADDIU t0,t0,1.
                              immediate(0x0e, 8, 9, 0x55),    // XORI t1,t0,0x55.
                              special(8, 9, 10, 0, 0x21),     // ADDU t2,t0,t1.
                              immediate(0x0c, 10, 11, 0xff),  // ANDI t3,t2,0xff.
                              immediate(0x0d, 11, 12, 0x100), // ORI t4,t3,0x100.
                              mtc0(14, 12),                   // MTC0 t6,Status: re-enable IE+IM2.
                              immediate(0x0d, 0, 15, 0x7777), // ORI t7,zero,0x7777: must not execute.
                          });
    system.cpu.gpr[8] = 1U;
    system.cpu.gpr[14] = 0x34000401U;
    system.cpu.gpr[15] = 0xfeedfacecafebeefULL;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
    system.cpu.write_cop0(12, 0x34000400U); // IM2 selected while IE remains clear.
}

void prepare_dma_cpu(System& system, u32 status) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.cpu.write_cop0(6, 7U);
    write_program(system, {
                              0U,
                              immediate(0x23, 16, 8, 0),    // LW t0,0(s0): dirty cached source.
                              immediate(0x2b, 16, 9, 4),    // SW t1,4(s0): stays in the CPU cache.
                              immediate(0x09, 10, 10, 1),   // ADDIU t2,t2,1.
                              immediate(0x0e, 8, 11, 0x55), // XORI t3,t0,0x55.
                              immediate(0x05, 10, 0, -5),   // BNE t2,zero,code+4.
                              immediate(0x0d, 12, 12, 1),   // ORI t4,t4,1 (delay slot).
                              0U,
                          });
    system.cpu.gpr[16] = cached_dma;
    system.cpu.gpr[9] = 0xcafebabeU;
    system.cpu.gpr[10] = 1U;
    system.cpu.fpu.control = (1U << 24U) | 3U;
    system.cpu.fpu.registers[2] = 0x0123456789abcdefULL;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system);
    warm_dma_cache_line(system);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4U);
    system.cpu.write_cop0(12, status);
}

void rsp_program(System& system, std::initializer_list<u32> instructions, u32 address = 0) {
    unsigned offset = address;
    for (const u32 instruction : instructions) {
        write_be32(system.rsp.memory.data() + 0x1000U + (offset & 0x0fffU), instruction);
        offset += 4;
    }
}

void start_rsp(System& system, u32 address = 0, u32 status_command = 1U) {
    system.rsp.write_pc(address);
    system.rsp.write_register(0x10, status_command);
}

void shared_clock_loop(System& system) {
    rsp_program(system, {
                            0x40026000U, // MFC0 v0,DPC_CLOCK.
                            0xac020080U, // SW v0,0x80(zero).
                            0x24630001U, // ADDIU v1,v1,1.
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
    for (const u32 offset : {0x00U, 0x04U, 0x08U, 0x0cU, 0x10U, 0x14U, 0x18U})
        CHECK_EQ(batched.rsp.read_register(offset), stepped.rsp.read_register(offset));

    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.bus.rdram_refresh_wait(), stepped.bus.rdram_refresh_wait());
    CHECK_EQ(batched.bus.rdp.read_register(0x0c), stepped.bus.rdp.read_register(0x0c));
    CHECK_EQ(batched.bus.rdp.read_register(0x10), stepped.bus.rdp.read_register(0x10));
    CHECK(batched.bus.rdram == stepped.bus.rdram);
}

unsigned compare_slice(System& batched, System& stepped, unsigned maximum_steps,
                       u64 maximum_cycles = 1'000'000U) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    equivalent(batched, stepped);
    return expected;
}

void short_video(System& system) {
    auto& bus = system.bus;
    bus.write(0x04400000, 4, 0x303U);
    bus.write(0x04400004, 4, 0x3000U);
    bus.write(0x04400008, 4, 16U);
    bus.write(0x0440000c, 4, 4U);
    bus.write(0x04400018, 4, 13U);
    bus.write(0x0440001c, 4, 99U);
    bus.write(0x04400020, 4, (100U << 16U) | 100U);
    bus.write(0x04400024, 4, (108U << 16U) | 111U);
    bus.write(0x04400028, 4, (2U << 16U) | 4U);
    bus.write(0x04400030, 4, 1024U);
    bus.write(0x04400034, 4, 1024U);
}

} // namespace

TEST(cpu_cached_masked_rsp_shared_clock_catchup_matches_all_masked_status_combinations_and_phases) {
    unsigned cases = 0;
    for (unsigned phase = 0; phase < 3U; ++phase) {
        for (unsigned bits = 0; bits < 16U; ++bits) {
            const bool ie = (bits & 1U) != 0;
            const bool exl = (bits & 2U) != 0;
            const bool erl = (bits & 4U) != 0;
            const bool im2 = (bits & 8U) != 0;
            if (ie && !exl && !erl && im2)
                continue;
            const u32 status =
                0x34000000U | (ie ? 1U : 0U) | (exl ? 2U : 0U) | (erl ? 4U : 0U) | (im2 ? 0x400U : 0U);
            CHECK((status & 0x407U) != 0x401U);

            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_cpu(*system, status);
                system->advance(phase);
                shared_clock_loop(*system);
            }

            constexpr unsigned slice = 32U;
            const u64 before = batched.cpu.batched_cached_instructions();
            CHECK_EQ(compare_slice(batched, stepped, slice), slice);
            CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
            CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
            CHECK(read_be32(batched.rsp.memory.data() + 0x84U) != 0U);
            ++cases;
        }
    }
    CHECK_EQ(cases, 45U);
}

TEST(cpu_cached_masked_rsp_normal_catchup_reconciles_single_step_and_raw_pc_shadow_changes) {
    for (unsigned mode = 0; mode < 2U; ++mode) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu(*system, 0x34000400U); // IE masks any RCP interrupt acceptance.
            if (mode == 0) {
                rsp_program(*system, {
                                         0x40026000U, // MFC0 v0,DPC_CLOCK.
                                         0xac020080U, // Would execute only if single-step continued.
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
                system->rsp.pc =
                    0x40U; // Direct host SP_PC change must invalidate the latched pair on catchup.
            }
        }

        constexpr unsigned slice = 48U;
        const u64 before = batched.cpu.batched_cached_instructions();
        CHECK_EQ(compare_slice(batched, stepped, slice), slice);
        CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
        if (mode == 0) {
            CHECK_EQ(batched.rsp.read_register(0x10U) & 0x21U, 0x21U); // Halted with single-step retained.
        } else {
            CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
            CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
        }
    }
}

TEST(cpu_cached_masked_rsp_encoded_status_reenable_accepts_pending_break_irq_before_younger_instruction) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_status_reenable_cpu(*system);
        system->rsp.write_register(0x10U, 1U << 8U); // Set interrupt-on-BREAK while still halted.
        rsp_program(*system, {0x0000000dU});         // BREAK on the first RSP issue boundary.
        start_rsp(*system);
        system->bus.write(0x0430000c, 4, 2U); // Enable the SP source in MI.
    }

    constexpr unsigned slice = 7U; // Five private ops, MTC0 Status, then interrupt acceptance.
    const u64 before = batched.cpu.batched_cached_instructions();
    CHECK_EQ(compare_slice(batched, stepped, slice), slice);
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before, 5U);
    CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x400U, 0x400U);
    CHECK(batched.cpu.exception_pending);
    CHECK_EQ(batched.cpu.cp0[14], code + 28U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, 0U);
    CHECK((batched.cpu.cp0[12] & 2U) != 0U);
    CHECK_EQ(batched.cpu.gpr[15], 0xfeedfacecafebeefULL);
}

TEST(cpu_cached_masked_rsp_dma_started_during_catchup_does_not_snoop_cpu_cache) {
    for (const bool to_sp : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_dma_cpu(*system, 0x34000001U); // IE set, IM2 clear.
            system->bus.write(0x200U, 4, 0x01020304U);
            system->bus.write(0x204U, 4, 0x05060708U);
            write_be32(system->rsp.memory.data() + 0x100U, 0xa0a1a2a3U);
            write_be32(system->rsp.memory.data() + 0x104U, 0xb0b1b2b3U);
            system->rsp.write_register(0x00U, 0x100U);
            system->rsp.write_register(0x04U, 0x200U);
            rsp_program(*system, {
                                     0x24010007U,                       // ADDIU at,zero,7: one DMA row.
                                     to_sp ? 0x40811000U : 0x40811800U, // MTC0 at,SP_RD_LEN/SP_WR_LEN.
                                     0x40023000U,                       // MFC0 v0,SP_DMA_BUSY.
                                     0x1440fffeU,                       // BNE v0,zero,previous MFC0.
                                     0U,
                                     0x40026000U, // MFC0 v0,DPC_CLOCK after completion.
                                     0xac020080U, // SW v0,0x80(zero).
                                     0x0000000dU,
                                 });
            start_rsp(*system);
        }

        constexpr unsigned slice = 96U;
        const u64 before = batched.cpu.batched_cached_instructions();
        CHECK_EQ(compare_slice(batched, stepped, slice), slice);
        CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
        CHECK_EQ(batched.cpu.gpr[8], 0x11223344U);
        const auto& cached = batched.cpu.data_cache[(cached_dma >> 4U) & 511U];
        CHECK(cached.valid && cached.dirty);
        CHECK_EQ(read_be32(cached.data.data()), 0x11223344U);
        CHECK_EQ(read_be32(cached.data.data() + 4U), 0xcafebabeU);
        if (to_sp) {
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x100U), 0x01020304U);
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x104U), 0x05060708U);
            CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x200U), 0x01020304U);
            CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x204U), 0x05060708U);
        } else {
            CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x200U), 0xa0a1a2a3U);
            CHECK_EQ(read_be32(batched.bus.rdram.data() + 0x204U), 0xb0b1b2b3U);
        }
        CHECK_EQ(batched.bus.memory.bank_access_clock(0x200U), stepped.bus.memory.bank_access_clock(0x200U));
        CHECK_EQ(batched.rsp.read_register(0x18U), 0U);
    }
}

TEST(cpu_cached_masked_rsp_restarts_local_catchup_between_dma_start_and_exact_transfer_cycle_load) {
    constexpr std::array<u32, 8> payload{
        0xa0a1a2a3U, 0xa4a5a6a7U, 0xb0b1b2b3U, 0xb4b5b6b7U,
        0xc0c1c2c3U, 0xc4c5c6c7U, 0xd0d1d2d3U, 0xd4d5d6d7U,
    };
    for (unsigned phase = 0; phase < 3U; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu(*system, 0x34000001U); // IE set, IM2 clear.
            system->advance(phase);
            for (unsigned index = 0; index < payload.size(); ++index)
                system->bus.write(0x200U + index * 4U, 4, payload[index]);
            for (u32 offset = 0; offset < 32U; offset += 4U)
                write_be32(system->rsp.memory.data() + 0x100U + offset, 0x11112222U + offset);
            system->rsp.write_register(0x00U, 0x100U);
            system->rsp.write_register(0x04U, 0x200U);
            rsp_program(*system,
                        {
                            0x8c020100U, // LW v0,0x100(zero): sample the old DMEM word.
                            0xac020080U, // SW v0,0x80(zero): preserve the old sample.
                            0x2401001fU, // ADDIU at,zero,31: one 32-byte DMA row.
                            0x40811000U, // MTC0 at,SP_RD_LEN: shared tick starts the DMA.
                            0U,          // Local cycle 1 before the four-cycle DMA deadline.
                            0U,          // Local cycle 2.
                            0U,          // Local cycle 3.
                            0x8c040100U, // LW a0,0x100(zero): transfer occurs before this exact cycle issues.
                            0xac040084U, // SW a0,0x84(zero): preserve the post-transfer sample.
                            0x0000000dU,
                        });
            start_rsp(*system);
        }

        constexpr unsigned slice = 48U;
        const u64 before = batched.cpu.batched_cached_instructions();
        CHECK_EQ(compare_slice(batched, stepped, slice), slice);
        CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80U), 0x11112222U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x84U), 0xa0a1a2a3U);
        for (unsigned index = 0; index < payload.size(); ++index)
            CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x100U + index * 4U), payload[index]);
        CHECK_EQ(batched.bus.memory.bank_access_clock(0x200U), stepped.bus.memory.bank_access_clock(0x200U));
        CHECK(batched.bus.memory.bank_access_clock(0x200U) != 0U);
        CHECK_EQ(batched.rsp.read_register(0x18U), 0U);
        CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
    }
}

TEST(cpu_cached_masked_rsp_rsp_started_imem_dma_replaces_future_code_before_jump) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_cpu(*system, 0x34000001U);          // IE set, IM2 clear.
        system->bus.write(0x4000U, 4, 0x40026000U); // New IMEM 0x40: MFC0 v0,DPC_CLOCK.
        system->bus.write(0x4004U, 4, 0xac020080U); // New IMEM 0x44: SW v0,0x80(zero).
        system->rsp.write_register(0x00U, 0x1040U);
        system->rsp.write_register(0x04U, 0x4000U);
        rsp_program(*system, {
                                 0x24010007U, // ADDIU at,zero,7: replace one eight-byte IMEM row.
                                 0x40811000U, // MTC0 at,SP_RD_LEN.
                                 0x40023000U, // MFC0 v0,SP_DMA_BUSY.
                                 0x1440fffeU, // BNE v0,zero,previous MFC0.
                                 0U,
                                 0x08000010U, // J 0x40 after the replacement is complete.
                                 0U,
                             });
        rsp_program(*system, {0U, 0U, 0x0000000dU}, 0x40U); // DMA replaces the first two words only.
        start_rsp(*system);
    }

    constexpr unsigned slice = 64U;
    const u64 before = batched.cpu.batched_cached_instructions();
    CHECK_EQ(compare_slice(batched, stepped, slice), slice);
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
    CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x1040U), 0x40026000U);
    CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x1044U), 0xac020080U);
    CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
    CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
    CHECK_EQ(batched.bus.memory.bank_access_clock(0x4000U), stepped.bus.memory.bank_access_clock(0x4000U));
}

TEST(cpu_cached_masked_rsp_rsp_dp_start_end_syncfull_and_clock_read_set_pending_irq) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_cpu(*system, 0x34000001U);                    // IE set, IM2 clear.
        system->bus.write(0x3000U, 8, 0x2900000000000000ULL); // SyncFull.
        system->bus.write(0x0430000cU, 4, 1U << 11U);         // Enable DP interrupt in MI.
        rsp_program(*system, {
                                 0x24013000U, // ADDIU at,zero,0x3000.
                                 0x40814000U, // MTC0 at,DPC_START.
                                 0x24013008U, // ADDIU at,zero,0x3008.
                                 0x40814800U, // MTC0 at,DPC_END: execute SyncFull.
                                 0x40026000U, // MFC0 v0,DPC_CLOCK after submission.
                                 0xac020080U, // SW v0,0x80(zero).
                                 0x0000000dU,
                             });
        start_rsp(*system);
    }

    constexpr unsigned slice = 64U;
    const u64 before = batched.cpu.batched_cached_instructions();
    CHECK_EQ(compare_slice(batched, stepped, slice), slice);
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before, slice);
    CHECK_EQ(batched.bus.rdp.current(), 0x3008U);
    CHECK((batched.bus.read(0x04300008U, 4) & 0x20U) != 0U);
    CHECK_EQ(batched.cpu.cp0[13] & 0x400U, 0x400U);
    CHECK(!batched.cpu.exception_pending);
    CHECK(read_be32(batched.rsp.memory.data() + 0x80U) != 0U);
    CHECK_EQ(batched.rsp.read_register(0x10U) & 3U, 3U);
    CHECK_EQ(batched.bus.memory.bank_access_clock(0x3000U), stepped.bus.memory.bank_access_clock(0x3000U));
}

TEST(cpu_cached_masked_rsp_callbacks_materialize_shared_rsp_before_video_audio_and_nmi) {
    using Observation = std::array<u64, 11>;
    System batched, stepped;
    std::vector<Observation> first, second;
    unsigned first_audio = 0;
    unsigned second_audio = 0;
    unsigned first_video = 0;
    unsigned second_video = 0;

    const auto attach = [](System& system, std::vector<Observation>& output, unsigned& audio,
                           unsigned& video) {
        prepare_cpu(system, 0x34000400U); // Keep maskable RCP interrupts from redirecting the CPU.
        shared_clock_loop(system);
        short_video(system);
        system.bus.write(0x04500010, 4, 99U);
        const auto observe = [machine = &system, observations = &output](u64 kind) {
            observations->push_back({kind, machine->cpu.cycles, machine->cpu.instruction_count,
                                     machine->cpu.pc, machine->cpu.next_pc, machine->cpu.cp0[9],
                                     machine->cpu.cp0[13], machine->rsp.pc,
                                     read_be32(machine->rsp.memory.data() + 0x80U),
                                     machine->bus.rdp.read_register(0x10), machine->bus.output_clock()});
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

TEST(cpu_cached_rsp_with_accepting_rcp_irq_keeps_shared_rsp_coupled) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare_cpu(*system, 0x34000401U); // IE and IM2 enabled, EXL/ERL clear.
        rsp_program(
            *system,
            {
                0x40026000U, // Shared DPC_CLOCK read at the first RSP boundary.
                0x40025800U, // DPC_STATUS is local; the first DPC_CLOCK still forces the shared boundary.
                0x40026000U,
                0x40025800U,
                0x0000000dU,
            });
        start_rsp(*system);
    }

    const u64 before = batched.cpu.batched_cached_instructions();
    compare_slice(batched, stepped, 2U);
    CHECK_EQ(batched.cpu.batched_cached_instructions() - before, 2U);
}
