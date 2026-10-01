#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <bit>
#include <memory>

using namespace cupid;

namespace {

constexpr u64 base = 0xffffffff80001000ULL;
constexpr std::array<u64, 10> values{0U,
                                     1U,
                                     0x7fffffffU,
                                     0x80000000U,
                                     0xffffffffU,
                                     0x100000000ULL,
                                     0x7fffffffffffffffULL,
                                     0x8000000000000000ULL,
                                     0xffffffff80000000ULL,
                                     0xffffffffffffffffULL};

u32 branch_word(unsigned kind, unsigned rs, unsigned rt, u16 offset) {
    return kind < 4U ? ((kind + 4U) << 26U) | (rs << 21U) | (rt << 16U) | offset
                     : (1U << 26U) | (rs << 21U) | ((kind - 4U) << 16U) | offset;
}

bool predicate(unsigned kind, u64 first, u64 second) {
    switch (kind) {
    case 0:
        return first == second;
    case 1:
        return first != second;
    case 2:
        return std::bit_cast<s64>(first) <= 0;
    case 3:
        return std::bit_cast<s64>(first) > 0;
    case 4:
        return std::bit_cast<s64>(first) < 0;
    default:
        return std::bit_cast<s64>(first) >= 0;
    }
}

void equivalent(System& first, System& second) {
    first.settle();
    second.settle();
    CHECK(first.cpu.gpr == second.cpu.gpr);
    CHECK(first.cpu.cp0 == second.cpu.cp0);
    CHECK_EQ(first.cpu.pc, second.cpu.pc);
    CHECK_EQ(first.cpu.next_pc, second.cpu.next_pc);
    CHECK_EQ(first.cpu.cycles, second.cpu.cycles);
    CHECK_EQ(first.cpu.instruction_count, second.cpu.instruction_count);
    CHECK_EQ(first.cpu.exception_pending, second.cpu.exception_pending);
    CHECK_EQ(first.cpu.frozen, second.cpu.frozen);
    CHECK_EQ(first.bus.output_clock(), second.bus.output_clock());
    CHECK_EQ(first.rsp.pc, second.rsp.pc);
    CHECK(first.rsp.memory == second.rsp.memory);
    for (unsigned index = 0; index < first.cpu.instruction_cache.size(); ++index) {
        const auto& a = first.cpu.instruction_cache[index];
        const auto& b = second.cpu.instruction_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
    }
    for (unsigned index = 0; index < first.cpu.data_cache.size(); ++index) {
        const auto& a = first.cpu.data_cache[index];
        const auto& b = second.cpu.data_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
        CHECK_EQ(a.dirty, b.dirty);
    }
}

void compare(System& native, System& ordinary, unsigned steps, u64 cycles = 1000000U) {
    unsigned retired = 0;
    const u64 start = ordinary.cpu.cycles;
    while (retired < steps && ordinary.cpu.cycles - start < cycles && !ordinary.cpu.frozen) {
        ordinary.cpu.step();
        ++retired;
    }
    CHECK_EQ(native.cpu.run_slice(steps, cycles), retired);
    equivalent(native, ordinary);
}

void prepare(System& system, unsigned kind, unsigned position, bool taken, bool backward) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    for (unsigned index = 0; index < 8U; ++index)
        system.bus.write(0x1000U + index * 4U, 4U, 0x25080001U); // ADDIU t0,t0,1.
    const u16 offset = backward ? static_cast<u16>(0U - position - 1U) : static_cast<u16>(8U - position - 1U);
    system.bus.write(0x1000U + position * 4U, 4U, branch_word(kind, 2U, 3U, offset));
    system.bus.write(0x1004U + position * 4U, 4U, 0x25290001U); // ADDIU t1,t1,1 in the delay slot.
    system.bus.write(0x1020U, 4U, 0x08000400U);
    system.bus.write(0x1024U, 4U, 0x254a0001U);
    system.cpu.gpr[3] = 0;
    system.cpu.gpr[2] = kind == 0U                 ? (taken ? 0U : 1U)
                        : kind == 1U || kind == 3U ? (taken ? 1U : 0U)
                        : kind == 2U               ? (taken ? 0U : 1U)
                        : kind == 4U               ? (taken ? ~0ULL : 0U)
                                                   : (taken ? 0U : ~0ULL);
    system.cpu.set_pc(base);
}

} // namespace

TEST(cpu_native_terminal_branch_predicates_preserve_wide_values_zero_and_register_aliases) {
    for (unsigned kind = 0; kind < 6U; ++kind) {
        for (unsigned rs : {0U, 1U, 2U, 31U}) {
            for (unsigned rt : {0U, 1U, 2U, 31U}) {
                const u32 word = branch_word(kind, rs, rt, 0x8000U);
                CHECK(CpuNativeCode::supports(word));
                CHECK(CpuNativeCode::terminal_branch(word));
                const auto compiled = CpuNativeCode::compile(std::span(&word, 1U));
                CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
                if (!compiled)
                    continue;
                for (u64 first : values) {
                    for (u64 second : values) {
                        std::array<u64, 32> registers{};
                        registers[1] = registers[31] = first;
                        registers[2] = second;
                        const auto before = registers;
                        CpuNativeState state(registers.data(), nullptr);
                        CHECK(compiled->execute(state));
                        CHECK_EQ(state.branch_taken != 0U, predicate(kind, registers[rs], registers[rt]));
                        CHECK(registers == before);
                    }
                }
            }
        }
    }
}

TEST(cpu_native_terminal_branches_close_blocks_and_reject_likely_or_link_operations) {
    for (unsigned kind = 0; kind < 6U; ++kind) {
        const u32 branch = branch_word(kind, 1U, 2U, 3U);
        const std::array allowed{0x24210001U, 0x24210001U, branch};
        CHECK_EQ(CpuNativeCode::compile(allowed) != nullptr, CpuNativeCode::available());
        const std::array rejected{0x24210001U, branch, 0x24210001U};
        CHECK(!CpuNativeCode::compile(rejected));
    }
    for (u32 word : {0x50000000U, 0x54000000U, 0x58000000U, 0x5c000000U, 0x04100000U, 0x04110000U,
                     0x04020000U, 0x04030000U, 0x04120000U, 0x04130000U}) {
        CHECK(!CpuNativeCode::terminal_branch(word));
        CHECK(!CpuNativeCode::supports(word));
    }
}

TEST(cpu_native_terminal_branches_match_forward_backward_targets_and_fragmented_delay_slots) {
    for (unsigned kind = 0; kind < 6U; ++kind) {
        for (unsigned position = 1; position < 7U; ++position) {
            for (bool taken : {false, true}) {
                for (bool backward : {false, true}) {
                    auto systems = std::make_unique<std::array<System, 2>>();
                    auto& [native, ordinary] = *systems;
                    prepare(native, kind, position, taken, backward);
                    prepare(ordinary, kind, position, taken, backward);
                    compare(native, ordinary, 512U);
                    CHECK_EQ(native.cpu.native_block_instructions() != 0U, CpuNativeCode::available());
                    for (unsigned budget = 0; budget < 15U; ++budget) {
                        compare(native, ordinary, budget);
                        compare(native, ordinary, 37U, budget);
                    }
                }
            }
        }
    }
}

TEST(cpu_native_terminal_branches_preserve_count_interrupt_epc_and_delay_slot_exceptions) {
    for (bool taken : {false, true}) {
        for (unsigned distance = 1; distance < 10U; ++distance) {
            auto systems = std::make_unique<std::array<System, 2>>();
            auto& [native, ordinary] = *systems;
            for (auto* system : {&native, &ordinary})
                prepare(*system, 0U, 3U, taken, false);
            compare(native, ordinary, 512U);
            for (auto* system : {&native, &ordinary}) {
                system->cpu.set_pc(base);
                system->cpu.write_cop0(11, static_cast<u32>(system->cpu.cp0[9]) + distance);
                system->cpu.write_cop0(12, 0x34008001U);
            }
            compare(native, ordinary, 24U);
            CHECK_EQ(native.cpu.cp0[13] & 0x8000U, 0x8000U);
        }
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        for (auto* system : {&native, &ordinary})
            prepare(*system, 0U, 3U, taken, false);
        compare(native, ordinary, 512U);
        for (auto* system : {&native, &ordinary}) {
            auto& line = system->cpu.instruction_cache[(base >> 5U) & 511U];
            write_be32(line.data.data() + 16U, 0x0000000cU); // SYSCALL in the delay slot.
            system->cpu.set_pc(base);
        }
        compare(native, ordinary, 5U);
        CHECK(native.cpu.exception_pending);
        CHECK_EQ(native.cpu.cp0[14], base + 12U);
        CHECK_EQ(native.cpu.cp0[13] & 0x80000000U, 0x80000000U);
    }
}

TEST(cpu_native_terminal_branch_interrupt_rollback_preserves_only_the_retired_prefix) {
    for (unsigned local_prefix = 0; local_prefix < 6U; ++local_prefix) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        for (auto* system : {&native, &ordinary})
            prepare(*system, 0U, 5U, true, false);
        compare(native, ordinary, 512U);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.set_pc(base);
            system->cpu.write_cop0(12, 0x34000401U);
            system->bus.write(0x0430000cU, 4U, 2U);
            for (unsigned index = 0; index < local_prefix; ++index)
                system->bus.write(0x04001000U + index * 4U, 4U, 0x24210001U);
            system->bus.write(0x04001000U + local_prefix * 4U, 4U, 0x0000000dU);
            system->rsp.write_register(0x10U, 0x101U);
        }
        compare(native, ordinary, 20U);
        CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
    }
}

TEST(cpu_native_terminal_branches_preserve_load_use_interlocks_and_staged_store_prefixes) {
    for (const bool store : {false, true}) {
        for (const bool taken : {false, true}) {
            auto systems = std::make_unique<std::array<System, 2>>();
            auto& [native, ordinary] = *systems;
            for (auto* system : {&native, &ordinary}) {
                prepare(*system, 0U, 3U, taken, false);
                system->cpu.gpr[4] = 0xffffffff80002000ULL;
                system->bus.write(0x2000U, 4U, taken ? 0U : 1U);
                system->bus.write(0x1008U, 4U,
                                  store ? 0xac880000U : 0x8c820000U); // SW t0,0(a0) or LW v0,0(a0).
            }
            compare(native, ordinary, 512U);
            CHECK_EQ(native.cpu.native_block_instructions() != 0U, CpuNativeCode::available());
            for (unsigned budget = 1; budget < 15U; ++budget) {
                compare(native, ordinary, budget);
                compare(native, ordinary, 31U, budget);
            }
            for (auto* system : {&native, &ordinary}) {
                system->cpu.set_pc(base);
                system->cpu.write_cop0(12, 0x34000401U);
                system->bus.write(0x0430000cU, 4U, 2U);
                system->bus.write(0x04001000U, 4U, 0x24210001U);
                system->bus.write(0x04001004U, 4U, 0x0000000dU);
                system->rsp.write_register(0x10U, 0x101U);
            }
            compare(native, ordinary, 8U);
            CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
        }
    }
}

namespace {
u32 jump_word(unsigned kind, unsigned rs, unsigned rd, u64 target) {
    return kind < 2U ? ((kind + 2U) << 26U) | ((static_cast<u32>(target) & 0x0fffffffU) >> 2U)
                     : (rs << 21U) | (rd << 11U) | (kind == 2U ? 8U : 9U);
}

void prepare_jump(System& system, unsigned kind, unsigned position, bool backward) {
    prepare(system, 0U, position, true, false);
    const u64 target = backward ? base : base + 32U;
    system.cpu.gpr[2] = target;
    system.bus.write(0x1000U + position * 4U, 4U, jump_word(kind, 2U, 31U, target));
}
} // namespace

TEST(cpu_native_terminal_jumps_preserve_targets_link_values_and_source_destination_aliases) {
    for (unsigned kind = 0; kind < 4U; ++kind) {
        for (unsigned rs : {0U, 1U, 2U, 31U}) {
            for (unsigned rd : {0U, 1U, 2U, 31U}) {
                for (u64 target : {0ULL, 0x0ffffffcULL, 0xffffffff80001000ULL}) {
                    const u32 word = jump_word(kind, rs, rd, target);
                    CHECK(CpuNativeCode::supports(word));
                    CHECK(CpuNativeCode::terminal_branch(word));
                    const auto compiled = CpuNativeCode::compile(std::span(&word, 1U));
                    CHECK_EQ(compiled != nullptr, CpuNativeCode::available());
                    if (!compiled)
                        continue;
                    for (u64 value : values) {
                        std::array<u64, 32> registers{};
                        registers[1] = registers[2] = registers[31] = value;
                        const auto initial = registers;
                        CpuNativeState state(registers.data(), nullptr);
                        state.branch_link = 0xffffffff80002008ULL;
                        CHECK(compiled->execute(state));
                        CHECK_EQ(state.branch_taken, 1U);
                        CHECK_EQ(state.branch_target, kind < 2U ? target & 0x0ffffffcULL : initial[rs]);
                        auto expected = initial;
                        if (kind == 1U)
                            expected[31] = state.branch_link;
                        if (kind == 3U && rd != 0U)
                            expected[rd] = state.branch_link;
                        CHECK(registers == expected);
                        if (kind == 1U || kind == 3U) {
                            auto plain = initial;
                            CHECK(!compiled->execute(plain));
                            CHECK(plain == initial);
                        }
                    }
                }
            }
        }
    }
}

TEST(cpu_native_terminal_jumps_match_repeated_links_targets_and_fragmented_delay_slots) {
    for (unsigned kind = 0; kind < 4U; ++kind) {
        for (unsigned position = 1; position < 7U; ++position) {
            for (bool backward : {false, true}) {
                auto systems = std::make_unique<std::array<System, 2>>();
                auto& [native, ordinary] = *systems;
                prepare_jump(native, kind, position, backward);
                prepare_jump(ordinary, kind, position, backward);
                compare(native, ordinary, 512U);
                CHECK_EQ(native.cpu.native_block_instructions() != 0U, CpuNativeCode::available());
                for (unsigned budget = 0; budget < 15U; ++budget) {
                    compare(native, ordinary, budget);
                    compare(native, ordinary, 37U, budget);
                }
            }
        }
    }
}

TEST(cpu_native_terminal_jump_links_commit_only_before_the_retired_rsp_interrupt_boundary) {
    for (unsigned kind = 0; kind < 4U; ++kind) {
        for (unsigned prefix = 0; prefix < 7U; ++prefix) {
            auto systems = std::make_unique<std::array<System, 2>>();
            auto& [native, ordinary] = *systems;
            for (auto* system : {&native, &ordinary})
                prepare_jump(*system, kind, 5U, false);
            compare(native, ordinary, 512U);
            for (auto* system : {&native, &ordinary}) {
                system->cpu.set_pc(base);
                system->cpu.write_cop0(12, 0x34000401U);
                system->bus.write(0x0430000cU, 4U, 2U);
                for (unsigned index = 0; index < prefix; ++index)
                    system->bus.write(0x04001000U + index * 4U, 4U, 0x24210001U);
                system->bus.write(0x04001000U + prefix * 4U, 4U, 0x0000000dU);
                system->rsp.write_register(0x10U, 0x101U);
            }
            compare(native, ordinary, 24U);
            CHECK_EQ(native.cpu.cp0[13] & 0x400U, 0x400U);
        }
    }
}

TEST(cpu_native_terminal_register_jumps_leave_target_fetch_faults_on_the_ordinary_path) {
    for (const u64 target : {0xffffffff80002001ULL, 0xffffffff84000000ULL, 0x0000000000400000ULL}) {
        auto systems = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *systems;
        for (auto* system : {&native, &ordinary})
            prepare_jump(*system, 3U, 3U, false);
        compare(native, ordinary, 512U);
        for (auto* system : {&native, &ordinary}) {
            system->cpu.gpr[2] = target;
            system->cpu.set_pc(base);
        }
        compare(native, ordinary, 6U);
        CHECK(native.cpu.frozen || native.cpu.exception_pending);
    }
}
