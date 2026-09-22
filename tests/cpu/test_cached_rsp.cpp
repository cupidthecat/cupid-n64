#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>
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

void write_program(System& system, std::initializer_list<u32> instructions) {
    const u32 base = static_cast<u32>(code) & 0x1fffffffU;
    unsigned offset = 0;
    for (const u32 instruction : instructions) {
        system.bus.write(base + offset, 4, instruction);
        offset += 4;
    }
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

void prepare_cpu(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    write_program(system, {
                              immediate(0x23, 16, 8, 0),     // LW t0,0(s0): cached load.
                              special(8, 0, 9, 0, 0x21),     // ADDU t1,t0,zero: load-use handoff.
                              immediate(0x09, 10, 10, 1),    // ADDIU t2,t2,1.
                              immediate(0x2b, 16, 10, 4),    // SW t2,4(s0): dirty D-cache hit.
                              immediate(0x0e, 10, 11, 0x55), // XORI t3,t2,0x55.
                              immediate(0x05, 10, 0, -6),    // BNE t2,zero,code.
                              immediate(0x0d, 12, 12, 1),    // ORI t4,t4,1 (delay slot).
                          });
    system.cpu.gpr[16] = data;
    system.cpu.gpr[10] = 1;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system, code);
    warm_data_cache(system);
}

void prepare_simple_cpu(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    write_program(system, {
                              0U,                          // Prologue used only to latch the hot loop.
                              immediate(0x09, 8, 8, 1),    // ADDIU t0,t0,1.
                              immediate(0x0e, 8, 9, 0x55), // XORI t1,t0,0x55.
                              immediate(0x05, 8, 0, -3),   // BNE t0,zero,code+4.
                              immediate(0x0d, 10, 10, 1),  // ORI t2,t2,1 (delay slot).
                          });
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system, code);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 4);
    CHECK_EQ(system.cpu.batched_cached_instructions(), 0U);
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
        write_be16(system.rsp.memory.data() + 0x100 + lane * 2, static_cast<u16>(lane + 1));
    rsp_program(system, {
                            0x24010100U, // ADDIU at,zero,0x100.
                            0xc8212000U, // LQV v1,0(at).
                            0x8c220040U, // LW v0,0x40(at).
                            0x24420001U, // ADDIU v0,v0,1.
                            0xac220040U, // SW v0,0x40(at).
                            0x4a010850U, // VADD v1,v1,v1.
                            0xe8212001U, // SQV v1,0x10(at).
                            0x1000fffaU, // BEQ zero,zero,0x08.
                            0U,
                        });
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
    CHECK_EQ(batched.cpu.hi, stepped.cpu.hi);
    CHECK_EQ(batched.cpu.lo, stepped.cpu.lo);
    CHECK_EQ(batched.cpu.exception_pending, stepped.cpu.exception_pending);
    CHECK_EQ(batched.cpu.frozen, stepped.cpu.frozen);
    CHECK_EQ(batched.cpu.linked, stepped.cpu.linked);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK_EQ(batched.cpu.read_cop0(1), stepped.cpu.read_cop0(1));
    CHECK_EQ(batched.cpu.read_cop0(9), stepped.cpu.read_cop0(9));
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.bus.rdram_refresh_wait(), stepped.bus.rdram_refresh_wait());
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10), stepped.rsp.read_register(0x10));
    CHECK(batched.rsp.memory == stepped.rsp.memory);
    for (unsigned index = 0; index < batched.cpu.data_cache.size(); ++index) {
        const auto& first = batched.cpu.data_cache[index];
        const auto& second = stepped.cpu.data_cache[index];
        CHECK(first.data == second.data);
        CHECK_EQ(first.tag, second.tag);
        CHECK_EQ(first.valid, second.valid);
        CHECK_EQ(first.dirty, second.dirty);
    }
}

void compare_slice(System& batched, System& stepped, unsigned maximum_steps, u64 maximum_cycles = 1'000'000) {
    const unsigned expected = stepped_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), expected);
    batched.settle();
    stepped.settle();
    equivalent(batched, stepped);
}

void future_steps(System& batched, System& stepped, unsigned count = 128) {
    for (unsigned index = 0; index < count; ++index) {
        batched.cpu.step();
        stepped.cpu.step();
    }
    batched.settle();
    stepped.settle();
    equivalent(batched, stepped);
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

} // namespace

TEST(cpu_cached_rsp_local_execution_matches_steps_for_all_phases_and_slice_sizes) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu(*system);
            system->advance(phase);
            local_rsp_loop(*system);
        }

        for (unsigned budget = 1; budget <= 64; ++budget)
            compare_slice(batched, stepped, budget);

        CHECK(batched.cpu.batched_cached_instructions() > 100U);
        const auto& dirty = batched.cpu.data_cache[(data >> 4) & 511U];
        CHECK(dirty.dirty);
        CHECK(read_be32(batched.rsp.memory.data() + 0x140) != 0U);
        future_steps(batched, stepped);
    }
}

TEST(cpu_cached_rsp_keeps_cached_sp_memory_independent_from_rsp_dmem) {
    constexpr u64 cached_sp = 0xffffffff84000140ULL;
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_cpu(*system);
            system->cpu.gpr[16] = cached_sp;
            auto& line = system->cpu.data_cache[(cached_sp >> 4) & 511U];
            line = {};
            line.valid = true;
            line.tag = 0x04000000U;
            write_be32(line.data.data(), 0x11223344U);
            system->advance(phase);
            local_rsp_loop(*system);
        }

        compare_slice(batched, stepped, 1024);
        CHECK(batched.cpu.batched_cached_instructions() > 100U);
        CHECK_EQ(batched.cpu.gpr[8], 0x11223344U);
        CHECK(read_be32(batched.rsp.memory.data() + 0x140) != 0U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x144), 0U);
        const auto& line = batched.cpu.data_cache[(cached_sp >> 4) & 511U];
        CHECK(line.dirty);
        CHECK(read_be32(line.data.data() + 4) != 0U);
        future_steps(batched, stepped);
    }
}

TEST(cpu_cached_rsp_stops_before_shared_control_ops_in_either_raw_slot) {
    constexpr u32 vnop = 0x4a000037U;
    constexpr u32 mfc0_dp_clock = 0x40025800U;
    constexpr u32 store_v0 = 0xac020080U;
    constexpr u32 break_instruction = 0x0000000dU;
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (unsigned shared_kind = 0; shared_kind < 2; ++shared_kind) {
            for (unsigned slot = 0; slot < 2; ++slot) {
                System batched, stepped;
                for (auto* system : {&batched, &stepped}) {
                    prepare_simple_cpu(*system);
                    system->advance(phase);
                    if (shared_kind == 0) {
                        if (slot == 0)
                            rsp_program(*system, {mfc0_dp_clock, store_v0, break_instruction});
                        else
                            rsp_program(*system, {vnop, mfc0_dp_clock, store_v0, break_instruction});
                    } else if (slot == 0) {
                        rsp_program(*system, {break_instruction});
                    } else {
                        rsp_program(*system, {vnop, break_instruction});
                    }
                }
                compare_slice(batched, stepped, 64);
                if (shared_kind == 0)
                    CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80),
                             read_be32(stepped.rsp.memory.data() + 0x80));
                future_steps(batched, stepped);
            }
        }
    }
}

TEST(cpu_cached_rsp_uses_latched_local_words_after_imem_changes_during_a_stall) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_simple_cpu(*system);
            system->advance(phase);
            rsp_program(*system, {
                                     0xc8012000U, // LQV v1,0(zero).
                                     0x4a010850U, // VADD v1,v1,v1.
                                     0x24020055U, // ADDIU v0,zero,0x55, paired behind the stall.
                                     0xac020080U, // SW v0,0x80(zero).
                                     0x0000000dU,
                                 });
            system->rsp.tick(1);
            system->rsp.tick(1);
            CHECK_EQ(system->rsp.pc, 4U);
            write_be32(system->rsp.memory.data() + 0x1004, 0x0000000dU);
            write_be32(system->rsp.memory.data() + 0x1008, 0x40025800U);
        }
        compare_slice(batched, stepped, 64);
        CHECK(batched.cpu.batched_cached_instructions() > 0U);
        CHECK_EQ(read_be32(batched.rsp.memory.data() + 0x80), 0x55U);
        CHECK_EQ(batched.rsp.read_register(0x10) & 3U, 3U);
        future_steps(batched, stepped);
    }
}

TEST(cpu_cached_rsp_can_retire_a_non_rcp_cpu_cycle_before_a_shared_rsp_tick) {
    for (unsigned advance = 0; advance < 3; ++advance) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_simple_cpu(*system); // Leaves the CPU-to-RCP phase at 2.
            system->advance(advance);
            rsp_program(*system, {0x0000000dU}); // BREAK must run only on an RCP tick.
        }
        const u64 previously_batched = batched.cpu.batched_cached_instructions();
        compare_slice(batched, stepped, 2);
        const u64 added = batched.cpu.batched_cached_instructions() - previously_batched;
        CHECK_EQ(added, advance == 2 ? 1U : 0U);
        future_steps(batched, stepped);
    }
}

TEST(cpu_cached_rsp_rejects_latched_shared_ops_dma_single_step_and_raw_pc_rewrites) {
    // A shared COP0 in the second slot remains visible even while a vector dependency
    // stalls the already-latched pair.
    {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_simple_cpu(*system);
            rsp_program(*system, {
                                     0xc8012000U, // LQV v1,0(zero).
                                     0x4a010850U, // VADD v1,v1,v1.
                                     0x40025800U, // MFC0 v0,DPC_CLOCK behind the stall.
                                     0xac020080U,
                                     0x0000000dU,
                                 });
            system->rsp.tick(1);
            system->rsp.tick(1);
        }
        compare_slice(batched, stepped, 64);
        future_steps(batched, stepped);
    }

    for (unsigned mode = 0; mode < 3; ++mode) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare_simple_cpu(*system);
            system->advance(2); // The prologue leaves phase 2; move to phase 0.
            local_rsp_loop(*system);
            if (mode == 0) {
                system->bus.write(0x2000, 4, 0x12345678U);
                system->rsp.write_register(0, 0x200);
                system->rsp.write_register(4, 0x2000);
                system->rsp.write_register(8, 31);
            } else if (mode == 1) {
                system->rsp.write_register(0x10, 0x41U); // Single step while running.
            } else {
                rsp_program(*system, {
                                         0xc8012000U,
                                         0x4a010850U,
                                         0x24420001U,
                                     });
                write_be32(system->rsp.memory.data() + 0x1040, 0x40025800U);
                write_be32(system->rsp.memory.data() + 0x1044, 0xac020080U);
                write_be32(system->rsp.memory.data() + 0x1048, 0x0000000dU);
                system->rsp.tick(1);
                system->rsp.tick(1);
                system->rsp.pc = 0x40; // Raw host rewrite must discard the latched pair.
            }
        }
        const u64 previously_batched = batched.cpu.batched_cached_instructions();
        compare_slice(batched, stepped, 2);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), previously_batched);
        future_steps(batched, stepped);
    }
}

TEST(cpu_cached_rsp_preserves_compare_and_vi_callback_boundaries) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const unsigned distance : {1U, 2U, 3U, 9U}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare_cpu(*system);
                system->advance(phase);
                local_rsp_loop(*system);
                system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + distance);
                system->cpu.write_cop0(12, 0x34008001U);
            }
            compare_slice(batched, stepped, 128);
            future_steps(batched, stepped);
        }
    }

    using Observation = std::array<u64, 7>;
    System batched, stepped;
    std::vector<Observation> first, second;
    const auto attach = [](System& system, std::vector<Observation>& output) {
        prepare_cpu(system);
        local_rsp_loop(system);
        short_video(system);
        system.bus.set_video_output([machine = &system, observations = &output](VideoField) {
            observations->push_back({machine->cpu.cycles, machine->cpu.instruction_count, machine->cpu.pc,
                                     machine->cpu.cp0[9], machine->rsp.pc,
                                     read_be32(machine->rsp.memory.data() + 0x140),
                                     machine->bus.output_clock()});
            if (observations->size() == 1)
                machine->cpu.request_nmi();
        });
    };
    attach(batched, first);
    attach(stepped, second);
    compare_slice(batched, stepped, 4096);
    CHECK(!first.empty());
    CHECK(first == second);
    future_steps(batched, stepped);
}
