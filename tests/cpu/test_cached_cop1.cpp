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
constexpr u64 data = 0xffffffff80002000ULL;

constexpr u32 immediate(unsigned op, unsigned rs, unsigned rt, int value) {
    return (op << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(value);
}

constexpr u32 cop1_transfer(unsigned operation, unsigned rt, unsigned fs) {
    return (0x11U << 26) | (operation << 21) | (rt << 16) | (fs << 11);
}

constexpr u32 cop1_format(unsigned format, unsigned ft, unsigned fs, unsigned fd, unsigned function) {
    return (0x11U << 26) | (format << 21) | (ft << 16) | (fs << 11) | (fd << 6) | function;
}

void write_program(System& system, std::initializer_list<u32> instructions) {
    u32 address = static_cast<u32>(code) & 0x1fffffffU;
    for (u32 instruction : instructions) {
        system.bus.write(address, 4, instruction);
        address += 4;
    }
}

void prepare(System& system, std::initializer_list<u32> instructions, bool full_registers = true,
             bool cop1_enabled = true) {
    test::initialize_memory(system);
    u32 status = 0x10000000U;
    if (cop1_enabled)
        status |= 0x20000000U;
    if (full_registers)
        status |= 0x04000000U;
    system.cpu.write_cop0(12, status);
    write_program(system, instructions);
    system.cpu.set_pc(code);
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
    CHECK_EQ(batched.cpu.exception_pending, stepped.cpu.exception_pending);
    CHECK_EQ(batched.cpu.frozen, stepped.cpu.frozen);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK(batched.cpu.fpu.registers == stepped.cpu.fpu.registers);
    CHECK_EQ(batched.cpu.fpu.control, stepped.cpu.fpu.control);
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
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

using Observation = std::array<u64, 5>;

} // namespace

TEST(cpu_cached_cop1_transfers_preserve_fr_aliases_cfc1_and_zero_register) {
    for (bool full_registers : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system,
                    {0U, cop1_transfer(0x00, 2, 3), cop1_transfer(0x01, 3, 5), cop1_transfer(0x02, 4, 31),
                     cop1_transfer(0x04, 5, 7), cop1_transfer(0x05, 6, 9), cop1_transfer(0x00, 0, 2),
                     cop1_transfer(0x04, 0, 11), 0x40074800U},
                    full_registers);
            warm_instruction_cache_line(*system, code);
            warm_instruction_cache_line(*system, code + 32);
            auto& fpu = system->cpu.fpu;
            for (unsigned index = 0; index < fpu.registers.size(); ++index)
                fpu.registers[index] = 0x1000000000000000ULL * (index & 7U) + 0x0102030405060708ULL + index;
            fpu.control = 0x01800003U;
            system->cpu.gpr[5] = 0xaabbccdd11223344ULL;
            system->cpu.gpr[6] = 0x0123456789abcdefULL;
            system->cpu.step();
        }

        const u32 expected_word = batched.cpu.fpu.read_word(3);
        const u64 expected_double = batched.cpu.fpu.read_doubleword(5);
        compare_slice(batched, stepped, 7);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 7U);
        CHECK_EQ(batched.cpu.gpr[0], 0U);
        CHECK_EQ(batched.cpu.gpr[2], sign_extend32(expected_word));
        CHECK_EQ(batched.cpu.gpr[3], expected_double);
        CHECK_EQ(batched.cpu.gpr[4], sign_extend32(0x01800003U));
        CHECK_EQ(batched.cpu.fpu.read_word(7), 0x11223344U);
        CHECK_EQ(batched.cpu.fpu.read_doubleword(9), 0x0123456789abcdefULL);
        CHECK_EQ(batched.cpu.fpu.read_word(11), 0U);
    }
}

TEST(cpu_cached_cop1_keeps_encoded_integer_load_interlock_and_cu1_priority) {
    {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system,
                    {0U, immediate(0x23, 1, 2, 0), cop1_transfer(0x00, 2, 4), immediate(0x0d, 0, 3, 7)});
            warm_instruction_cache_line(*system, code);
            warm_data_cache(*system);
            system->cpu.gpr[1] = data;
            system->cpu.fpu.registers[4] = 0x12345678U;
            system->cpu.step();
        }
        compare_slice(batched, stepped, 3);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 1U);
        CHECK_EQ(batched.cpu.gpr[2], 0x12345678U);
    }

    for (u32 candidate :
         {cop1_transfer(0x00, 0, 2), cop1_format(0x10, 4, 2, 0, 0x32), immediate(0x31, 1, 3, 2)}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, candidate, immediate(0x0d, 0, 3, 7)}, true, false);
            warm_instruction_cache_line(*system, code);
            warm_data_cache(*system);
            system->cpu.gpr[1] = data;
            system->cpu.fpu.registers[2] = 0x7fc00000U;
            system->cpu.fpu.registers[4] = 0x3f800000U;
            system->cpu.fpu.control = 1U << 11U;
            system->cpu.step();
        }
        compare_slice(batched, stepped, 2, 2);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 0U);
        CHECK_EQ((batched.cpu.cp0[13] >> 2U) & 31U, static_cast<u64>(Exception::CoprocessorUnusable));
        CHECK_EQ((batched.cpu.cp0[13] >> 28U) & 3U, 1U);
    }
}

TEST(cpu_cached_cop1_compares_match_all_predicates_and_recheck_live_nan_traps) {
    for (unsigned format : {0x10U, 0x11U}) {
        const u64 one = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
        const u64 two = format == 0x10U ? 0x40000000ULL : 0x4000000000000000ULL;
        const u64 nan = format == 0x10U ? 0x7fc00000ULL : 0x7ff8000000000000ULL;
        for (unsigned predicate = 0; predicate < 16U; ++predicate) {
            for (bool unordered : {false, true}) {
                System batched, stepped;
                for (auto* system : {&batched, &stepped}) {
                    prepare(*system, {0U, cop1_format(format, 4, 2, 0, 0x30U + predicate),
                                      immediate(0x0d, 0, 8, 7), 0x40094800U});
                    warm_instruction_cache_line(*system, code);
                    system->cpu.fpu.registers[2] = unordered ? nan : one;
                    system->cpu.fpu.registers[4] = two;
                    system->cpu.fpu.control = 0;
                    system->cpu.step();
                }
                compare_slice(batched, stepped, 2);
                CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
            }
        }
    }

    System live_batched, live_stepped;
    for (auto* system : {&live_batched, &live_stepped}) {
        prepare(*system,
                {0U, cop1_transfer(0x04, 2, 4), cop1_format(0x10, 6, 4, 0, 0x32), immediate(0x0d, 0, 8, 7)});
        warm_instruction_cache_line(*system, code);
        system->cpu.gpr[2] = 0x7fc00000U;
        system->cpu.fpu.registers[4] = 0x3f800000U;
        system->cpu.fpu.registers[6] = 0x3f800000U;
        system->cpu.fpu.control = (1U << 23U) | (1U << 11U);
        system->cpu.step();
    }
    compare_slice(live_batched, live_stepped, 2);
    CHECK_EQ(live_batched.cpu.batched_cached_instructions(), 1U);
    CHECK(live_batched.cpu.exception_pending);
    CHECK_EQ(live_batched.cpu.fpu.control & (1U << 23U), 1U << 23U);
    CHECK_EQ(live_batched.cpu.fpu.control & (1U << 16U), 1U << 16U);
}

TEST(cpu_cached_cop1_compare_preflight_distinguishes_nan_trap_policy) {
    for (unsigned format : {0x10U, 0x11U})
        for (bool signaling : {false, true}) {
            System batched, stepped;
            for (auto* system : {&batched, &stepped}) {
                prepare(*system, {0U, cop1_format(format, 4, 2, 0, signaling ? 0x3aU : 0x32U),
                                  immediate(0x0d, 0, 8, 7)});
                warm_instruction_cache_line(*system, code);
                system->cpu.fpu.registers[2] = format == 0x10U ? 0x7f800001ULL : 0x7ff0000000000001ULL;
                system->cpu.fpu.registers[4] = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
                system->cpu.fpu.control = 1U << 11U;
                system->cpu.step();
            }

            compare_slice(batched, stepped, 2, 2);
            CHECK_EQ(batched.cpu.batched_cached_instructions(), signaling ? 0U : 2U);
            CHECK_EQ(batched.cpu.exception_pending, signaling);
            CHECK_EQ((batched.cpu.fpu.control & (1U << 16U)) != 0, signaling);
        }
}

TEST(cpu_cached_cop1_compares_remain_single_cycle_at_slice_boundary) {
    for (unsigned format : {0x10U, 0x11U}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, cop1_format(format, 4, 2, 0, 0x32U), cop1_format(format, 4, 2, 0, 0x32U),
                              immediate(0x0d, 0, 8, 7)});
            warm_instruction_cache_line(*system, code);
            system->cpu.fpu.registers[2] = 0;
            system->cpu.fpu.registers[4] = 0;
            system->cpu.step();
        }

        const u64 start_cycles = batched.cpu.cycles;
        compare_slice(batched, stepped, 3, 2);
        CHECK_EQ(batched.cpu.cycles - start_cycles, 2U);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
        CHECK_EQ(batched.cpu.pc, code + 12U);
    }
}

TEST(cpu_cached_cop1_compare_preflight_uses_exact_fr0_fs_and_ft_sources) {
    {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, cop1_format(0x10, 4, 3, 0, 0x32), immediate(0x0d, 0, 8, 7), 0x40094800U},
                    false);
            warm_instruction_cache_line(*system, code);
            system->cpu.fpu.registers[2] = 0x3f800000U;
            system->cpu.fpu.registers[3] = 0x7fc00000U;
            system->cpu.fpu.registers[4] = 0x3f800000U;
            system->cpu.fpu.control = 1U << 11U;
            system->cpu.step();
        }
        compare_slice(batched, stepped, 2);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 2U);
    }

    {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system, {0U, cop1_format(0x10, 5, 2, 0, 0x32), immediate(0x0d, 0, 8, 7), 0x40094800U},
                    false);
            warm_instruction_cache_line(*system, code);
            system->cpu.fpu.registers[2] = 0x3f800000U;
            system->cpu.fpu.registers[4] = 0x3f800000U;
            system->cpu.fpu.registers[5] = 0x7fc00000U;
            system->cpu.fpu.control = 1U << 11U;
            system->cpu.step();
        }
        compare_slice(batched, stepped, 2, 2);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 0U);
        CHECK(batched.cpu.exception_pending);
    }
}

TEST(cpu_cached_cop1_replaces_blanket_pending_fpu_reject_with_exact_hazard) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        System batched, stepped;
        const u32 second = mode == 0   ? cop1_transfer(0x00, 8, 6)
                           : mode == 1 ? cop1_format(0x10, 4, 6, 0, 0x32)
                                       : cop1_format(0x10, 6, 4, 0, 0x32);
        for (auto* system : {&batched, &stepped}) {
            prepare(*system,
                    {cop1_format(0x10, 0, 2, 6, 0x06), second, immediate(0x0d, 0, 9, 7), 0x400a4800U});
            warm_instruction_cache_line(*system, code);
            system->cpu.fpu.registers[2] = 0x3f800000U;
            system->cpu.fpu.registers[4] = 0x3f800000U;
            system->cpu.step();
        }
        compare_slice(batched, stepped, 2);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), mode == 0 ? 2U : 0U);
        CHECK_EQ(batched.cpu.gpr[8], mode == 0 ? sign_extend32(0x3f800000U) : 0U);
    }
}

TEST(cpu_cached_cop1_memory_hits_preserve_fr_mapping_and_cache_only_stores) {
    for (bool full_registers : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system,
                    {0U, immediate(0x31, 1, 3, 0), immediate(0x39, 1, 3, 4), immediate(0x35, 1, 5, 8),
                     immediate(0x3d, 1, 5, 0), 0x40064800U},
                    full_registers);
            warm_instruction_cache_line(*system, code);
            warm_data_cache(*system);
            system->cpu.gpr[1] = data;
            system->cpu.fpu.registers[2] = 0x123456789abcdef0ULL;
            system->cpu.fpu.registers[3] = 0xdeadbeefcafebabeULL;
            system->cpu.fpu.registers[4] = 0x1111111122222222ULL;
            system->cpu.fpu.registers[5] = 0x3333333344444444ULL;
            system->cpu.step();
        }
        std::array<u8, 16> backing{};
        for (unsigned index = 0; index < backing.size(); ++index)
            backing[index] = batched.bus.rdram[0x2000U + index];

        compare_slice(batched, stepped, 4);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 4U);
        CHECK_EQ(batched.cpu.fpu.read_word(3), 0x11223344U);
        CHECK_EQ(batched.cpu.fpu.read_doubleword(5), 0x99aabbccddeeff00ULL);
        if (full_registers)
            CHECK_EQ(batched.cpu.fpu.registers[3] >> 32U, 0xdeadbeefULL);
        const auto& line = batched.cpu.data_cache[(data >> 4) & 511U];
        CHECK(line.dirty);
        CHECK_EQ(read_be64(line.data.data()), 0x99aabbccddeeff00ULL);
        for (unsigned index = 0; index < backing.size(); ++index)
            CHECK_EQ(batched.bus.rdram[0x2000U + index], backing[index]);
    }
}

TEST(cpu_cached_cop1_memory_rechecks_live_base_and_treats_fpr_zero_as_a_register) {
    constexpr u64 second = data + 16U;
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system, {0U, cop1_transfer(0x00, 1, 2), immediate(0x31, 1, 0, 0), immediate(0x39, 1, 0, 4),
                          0x40034800U});
        warm_instruction_cache_line(*system, code);
        system->cpu.gpr[1] = data;
        system->cpu.fpu.registers[2] = static_cast<u32>(second);
        auto& line = system->cpu.data_cache[(second >> 4) & 511U];
        line = {};
        line.valid = true;
        line.tag = 0x2000U;
        write_be32(line.data.data(), 0xcafebabeU);
        system->cpu.step();
    }
    compare_slice(batched, stepped, 3);
    CHECK_EQ(batched.cpu.batched_cached_instructions(), 3U);
    CHECK_EQ(batched.cpu.gpr[1], second);
    CHECK_EQ(batched.cpu.fpu.read_word(0), 0xcafebabeU);
    const auto& line = batched.cpu.data_cache[(second >> 4) & 511U];
    CHECK(line.dirty);
    CHECK_EQ(read_be32(line.data.data() + 4), 0xcafebabeU);
}

TEST(cpu_cached_cop1_memory_falls_back_for_cold_misaligned_external_and_cu1_off_accesses) {
    enum class Case { Cold, Misaligned, External, Cu1Off };
    for (Case kind : {Case::Cold, Case::Misaligned, Case::External, Case::Cu1Off}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            const bool cu1 = kind != Case::Cu1Off;
            const u32 instruction = kind == Case::Misaligned ? immediate(0x35, 1, 3, 4)
                                    : kind == Case::External ? immediate(0x35, 1, 3, 0)
                                                             : immediate(0x31, 1, 3, 0);
            prepare(*system, {0U, instruction, immediate(0x0d, 0, 8, 7)}, true, cu1);
            warm_instruction_cache_line(*system, code);
            if (kind != Case::Cold)
                warm_data_cache(*system);
            system->cpu.gpr[1] = kind == Case::External ? 0xffffffff84002000ULL : data;
            if (kind == Case::External) {
                auto& line = system->cpu.data_cache[(system->cpu.gpr[1] >> 4) & 511U];
                line.valid = true;
                line.tag = 0x04002000U;
            }
            system->cpu.fpu.registers[3] = 0x123456789abcdef0ULL;
            system->cpu.step();
        }
        compare_slice(batched, stepped, 2, 2);
        CHECK_EQ(batched.cpu.batched_cached_instructions(), 0U);
        if (kind == Case::Misaligned)
            CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, static_cast<u32>(Exception::AddressLoad) << 2U);
        else if (kind == Case::External)
            CHECK(batched.cpu.frozen);
        else if (kind == Case::Cu1Off)
            CHECK_EQ((batched.cpu.cp0[13] >> 2U) & 31U, static_cast<u64>(Exception::CoprocessorUnusable));
    }
}

TEST(cpu_cached_cop1_compare_preflight_rechecks_nan_and_fcsr_after_callbacks) {
    for (unsigned format : {0x10U, 0x11U})
        for (unsigned mutation = 0; mutation < 2; ++mutation) {
            System batched, stepped;
            unsigned batched_callbacks = 0;
            unsigned stepped_callbacks = 0;
            const u64 one = format == 0x10U ? 0x3f800000ULL : 0x3ff0000000000000ULL;
            const u64 nan = format == 0x10U ? 0x7fc00000ULL : 0x7ff8000000000000ULL;
            const auto attach = [&](System& system, unsigned& callbacks) {
                prepare(system, {0U, cop1_format(format, 4, 2, 0, 0x32U), immediate(0x05, 8, 0, -2), 0U});
                warm_instruction_cache_line(system, code);
                system.cpu.gpr[8] = 1;
                system.cpu.fpu.registers[2] = mutation == 0 ? one : nan;
                system.cpu.fpu.registers[4] = one;
                system.cpu.fpu.control = mutation == 0 ? 1U << 11U : 0U;
                short_video(system);
                system.bus.set_video_output(
                    [machine = &system, count = &callbacks, mutation, nan](VideoField) {
                        ++*count;
                        if (*count != 1)
                            return;
                        if (mutation == 0)
                            machine->cpu.fpu.registers[2] = nan;
                        else
                            machine->cpu.fpu.control |= 1U << 11U;
                    });
                system.cpu.step();
            };

            attach(batched, batched_callbacks);
            attach(stepped, stepped_callbacks);
            compare_slice(batched, stepped, 4096);
            CHECK(batched_callbacks != 0U);
            CHECK_EQ(batched_callbacks, stepped_callbacks);
            CHECK_EQ(batched.cpu.cp0[13] & 0x7cU, static_cast<u32>(Exception::FloatingPoint) << 2U);
            CHECK_EQ(batched.cpu.fpu.control & (1U << 16U), 1U << 16U);
        }
}

TEST(cpu_cached_cop1_preserves_device_and_local_rsp_boundaries) {
    const auto program = {cop1_transfer(0x04, 8, 2), cop1_format(0x10, 4, 2, 0, 0x32),
                          cop1_transfer(0x00, 9, 2), cop1_transfer(0x02, 10, 31),
                          immediate(0x05, 8, 0, -5), immediate(0x0d, 11, 11, 1)};

    System batched, stepped;
    std::vector<Observation> first, second;
    const auto attach = [&](System& system, std::vector<Observation>& observations) {
        prepare(system, program);
        warm_instruction_cache_line(system, code);
        system.cpu.gpr[8] = 0x3f800000U;
        system.cpu.fpu.registers[4] = 0x3f800000U;
        short_video(system);
        system.bus.set_video_output([&system, &observations](VideoField) {
            observations.push_back({system.cpu.cycles, system.cpu.instruction_count, system.cpu.pc,
                                    system.cpu.fpu.control, system.cpu.gpr[9]});
        });
    };
    attach(batched, first);
    attach(stepped, second);
    compare_slice(batched, stepped, 4096);
    CHECK(!first.empty());
    CHECK(first == second);
    CHECK(batched.cpu.batched_cached_instructions() != 0U);

    System rsp_batched, rsp_stepped;
    for (auto* system : {&rsp_batched, &rsp_stepped}) {
        prepare(*system, program);
        warm_instruction_cache_line(*system, code);
        system->cpu.gpr[8] = 0x3f800000U;
        system->cpu.fpu.registers[4] = 0x3f800000U;
        system->cpu.step();
        system->rsp.write_pc(0);
        system->rsp.write_register(0x10, 1U);
    }
    compare_slice(rsp_batched, rsp_stepped, 16);
    CHECK_EQ(rsp_batched.cpu.batched_cached_instructions(), 16U);
}
