#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <cfenv>
#include <memory>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u32 arithmetic(unsigned format, unsigned ft, unsigned fs, unsigned fd, unsigned function) {
    return (0x11U << 26U) | (format << 21U) | (ft << 16U) | (fs << 11U) | (fd << 6U) | function;
}

void prepare(System& system, bool wide, bool paired = false, unsigned rounding = 0) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, paired ? 0x30000000U : 0x34000000U);
    system.cpu.fpu.control = 0x01000800U | rounding;
    system.cpu.fpu.registers[2] = wide ? 0x3ff8000000000000ULL : 0x3fc00000ULL; // 1.5
    system.cpu.fpu.registers[4] = wide ? 0x4004000000000000ULL : 0x40200000ULL; // 2.5
    const unsigned format = wide ? 17U : 16U;
    const std::array<u32, 8> words{
        0U,
        arithmetic(format, 4, 2, 6, 0),   // 1.5 + 2.5 = 4; three cycles.
        arithmetic(format, 2, 6, 8, 2),   // 4 * 1.5 = 6; shortcut plus result wait: three.
        arithmetic(format, 2, 8, 10, 1),  // 6 - 1.5 = 4.5; result wait: four.
        arithmetic(format, 2, 10, 12, 3), // 4.5 / 1.5 = 3; 30 single or 59 double cycles.
        0x1000fffbU,
        0U,
        0U, // Repeat at code+4, with a NOP delay slot.
    };
    auto& line = system.cpu.instruction_cache[(code >> 5U) & 511U];
    line = {};
    line.valid = true;
    line.tag = 0x1000U;
    for (unsigned index = 0; index < words.size(); ++index) {
        write_be32(line.data.data() + index * 4U, words[index]);
        system.bus.write(0x1000U + index * 4U, 4, words[index]);
    }
    system.cpu.set_pc(code);
    system.cpu.step();
}

unsigned step_slice(System& system, unsigned maximum_steps, u64 maximum_cycles) {
    unsigned steps = 0;
    const u64 start = system.cpu.cycles;
    while (steps < maximum_steps && system.cpu.cycles - start < maximum_cycles && !system.cpu.frozen) {
        system.cpu.step();
        ++steps;
    }
    system.settle();
    return steps;
}

void compare(System& batched, System& stepped, unsigned maximum_steps, u64 maximum_cycles = 10000U) {
    const unsigned count = step_slice(stepped, maximum_steps, maximum_cycles);
    CHECK_EQ(batched.cpu.run_slice(maximum_steps, maximum_cycles), count);
    CHECK_EQ(batched.cpu.cycles, stepped.cpu.cycles);
    CHECK_EQ(batched.cpu.instruction_count, stepped.cpu.instruction_count);
    CHECK_EQ(batched.cpu.pc, stepped.cpu.pc);
    CHECK_EQ(batched.cpu.next_pc, stepped.cpu.next_pc);
    CHECK_EQ(batched.cpu.exception_pending, stepped.cpu.exception_pending);
    CHECK(batched.cpu.gpr == stepped.cpu.gpr);
    CHECK(batched.cpu.cp0 == stepped.cpu.cp0);
    CHECK(batched.cpu.fpu.registers == stepped.cpu.fpu.registers);
    CHECK_EQ(batched.cpu.fpu.control, stepped.cpu.fpu.control);
    CHECK_EQ(batched.bus.output_clock(), stepped.bus.output_clock());
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10), stepped.rsp.read_register(0x10));
    CHECK(batched.rsp.memory == stepped.rsp.memory);
}

void start_rsp(System& system, bool dma) {
    const std::array<u32, 8> words{0x24210001U, 0xac010080U, 0x24420001U, 0xac020084U,
                                   0x40036000U, 0xac030088U, 0x1000fff9U, 0U};
    for (unsigned index = 0; index < words.size(); ++index)
        write_be32(system.rsp.memory.data() + 0x1000U + index * 4U, words[index]);
    system.rsp.write_pc(0);
    system.rsp.write_register(0x10, 1U);
    if (dma) {
        for (unsigned byte = 0; byte < 32U; ++byte)
            system.bus.write_ram_byte(0x400U + byte, static_cast<u8>(0x80U + byte));
        system.rsp.write_register(0x00, 0x300U);
        system.rsp.write_register(0x04, 0x400U);
        system.rsp.write_register(0x08, 31U);
    }
}

void prepare_conversion(System& system, bool paired, unsigned format, unsigned function, u64 input,
                        u32 control) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, paired ? 0x30000000U : 0x34000000U);
    system.cpu.fpu.control = control;
    system.cpu.fpu.registers.fill(0xdeadbeef11223344ULL);
    system.cpu.fpu.registers[paired ? 2 : 3] = input;
    const std::array<u32, 8> words{
        0U,
        arithmetic(format, 4, 3, 6, function),
        arithmetic(16, 0, 6, 8, 6),                  // Consume the conversion's pending result.
        (0x11U << 26U) | (10U << 16U) | (8U << 11U), // MFC1.
        0x254b0001U,                                 // ADDIU consumes the MFC1 load.
        0x1000fffbU,
        0U,
        0U,
    };
    auto& line = system.cpu.instruction_cache[(code >> 5U) & 511U];
    line = {};
    line.valid = true;
    line.tag = 0x1000U;
    for (unsigned index = 0; index < words.size(); ++index) {
        write_be32(line.data.data() + index * 4U, words[index]);
        system.bus.write(0x1000U + index * 4U, 4, words[index]);
    }
    system.cpu.set_pc(code);
    system.cpu.step();
}

} // namespace

TEST(cpu_cached_arithmetic_keeps_exact_results_interlocks_and_separate_cycle_totals) {
    for (bool wide : {false, true})
        for (bool paired : {false, true})
            for (unsigned rounding = 0; rounding < 4U; ++rounding) {
                auto batched = std::make_unique<System>();
                auto stepped = std::make_unique<System>();
                prepare(*batched, wide, paired, rounding);
                prepare(*stepped, wide, paired, rounding);
                const u64 start = batched->cpu.cycles;
                compare(*batched, *stepped, 4U);
                CHECK_EQ(batched->cpu.batched_cached_instructions(), 4U);
                CHECK_EQ(batched->cpu.cycles - start, wide ? 69U : 40U);
                CHECK_EQ(batched->cpu.fpu.registers[6], wide ? 0x4010000000000000ULL : 0x40800000ULL);
                CHECK_EQ(batched->cpu.fpu.registers[8], wide ? 0x4018000000000000ULL : 0x40c00000ULL);
                CHECK_EQ(batched->cpu.fpu.registers[10], wide ? 0x4012000000000000ULL : 0x40900000ULL);
                CHECK_EQ(batched->cpu.fpu.registers[12], wide ? 0x4008000000000000ULL : 0x40400000ULL);
                compare(*batched, *stepped, 3U); // Continue through branch, delay, and another ADD.
            }
}

TEST(cpu_cached_conversions_preserve_results_flags_fr_and_integer_fpu_interlocks) {
    for (const bool paired : {false, true}) {
        for (unsigned rounding = 0; rounding < 4U; ++rounding) {
            for (unsigned format : {0x10U, 0x11U, 0x14U, 0x15U}) {
                const bool floating_input = format < 0x14U;
                const u64 input = format == 0x10U   ? 0xc0200000ULL
                                  : format == 0x11U ? 0xc004000000000000ULL
                                                    : 16777217ULL;
                const std::vector<unsigned> functions =
                    floating_input ? std::vector<unsigned>{8, 9, 10, 11, 12, 13, 14, 15, 0x24, 0x25}
                                   : std::vector<unsigned>{0x20, 0x21};
                for (const unsigned function : functions) {
                    auto batched = std::make_unique<System>();
                    auto stepped = std::make_unique<System>();
                    const u32 control = rounding | 0x0003f000U | (1U << 5U) | (1U << 23U);
                    prepare_conversion(*batched, paired, format, function, input, control);
                    prepare_conversion(*stepped, paired, format, function, input, control);
                    compare(*batched, *stepped, 4U);
                    CHECK_EQ(batched->cpu.batched_cached_instructions(), 4U);
                    compare(*batched, *stepped, 3U);
                }
            }
        }
    }
}

TEST(cpu_cached_conversions_keep_cycle_budgets_and_rsp_dma_phases) {
    for (unsigned format : {0x10U, 0x15U}) {
        for (const u64 budget : {1ULL, 4ULL, 5ULL, 6ULL, 7ULL, 8ULL, 9ULL}) {
            for (unsigned phase = 0; phase < 3U; ++phase) {
                auto batched = std::make_unique<System>();
                auto stepped = std::make_unique<System>();
                for (auto* system : {batched.get(), stepped.get()}) {
                    prepare_conversion(*system, false, format, format == 0x10U ? 0x0cU : 0x20U,
                                       format == 0x10U ? 0x40200000ULL : 16777217ULL, 0);
                    system->advance(phase);
                    start_rsp(*system, true);
                }
                compare(*batched, *stepped, 4U, budget);
                compare(*batched, *stepped, 24U);
                for (unsigned byte = 0; byte < 32U; ++byte)
                    CHECK_EQ(batched->rsp.memory[0x300U + byte], static_cast<u8>(0x80U + byte));
            }
        }
    }
}

TEST(cpu_cached_conversions_leave_enabled_exceptions_and_unimplemented_inputs_on_step) {
    using Case = std::array<u64, 4>;
    constexpr std::array<Case, 6> cases{
        Case{0x10U, 0x0cU, 0x40200000U, 0x80U},
        Case{0x11U, 0x25U, 0x7ff8000000000000ULL, 0U},
        Case{0x10U, 0x24U, 1U, 0U},
        Case{0x14U, 0x20U, 16777217U, 0x80U},
        Case{0x15U, 0x21U, 0x0020000000000001ULL, 0x80U},
        Case{0x15U, 0x20U, 0x0080000000000000ULL, 0U},
    };
    for (const auto& example : cases) {
        auto batched = std::make_unique<System>();
        auto stepped = std::make_unique<System>();
        const auto format = static_cast<unsigned>(example[0]);
        const auto function = static_cast<unsigned>(example[1]);
        prepare_conversion(*batched, false, format, function, example[2], static_cast<u32>(example[3]));
        prepare_conversion(*stepped, false, format, function, example[2], static_cast<u32>(example[3]));
        CHECK_EQ(batched->cpu.fpu.nontrapping_arithmetic_cycles(arithmetic(format, 4, 3, 6, function)), 0U);
        compare(*batched, *stepped, 1U);
        CHECK_EQ(batched->cpu.batched_cached_instructions(), 0U);
        CHECK_EQ(batched->cpu.fpu.registers[6], 0xdeadbeef11223344ULL);
        compare(*batched, *stepped, 3U);
    }
}

TEST(cpu_cached_arithmetic_preserves_budgets_that_end_inside_an_instruction) {
    constexpr std::array<u64, 10> budgets{1U, 2U, 3U, 4U, 5U, 9U, 10U, 11U, 39U, 68U};
    for (bool wide : {false, true})
        for (const u64 budget : budgets) {
            auto batched = std::make_unique<System>();
            auto stepped = std::make_unique<System>();
            prepare(*batched, wide);
            prepare(*stepped, wide);
            compare(*batched, *stepped, 4U, budget);
            compare(*batched, *stepped, 2U);
        }
}

TEST(cpu_cached_arithmetic_preserves_rsp_shared_reads_and_dma_at_every_clock_phase) {
    for (bool wide : {false, true})
        for (bool dma : {false, true})
            for (unsigned phase = 0; phase < 3U; ++phase) {
                auto batched = std::make_unique<System>();
                auto stepped = std::make_unique<System>();
                for (auto* system : {batched.get(), stepped.get()}) {
                    prepare(*system, wide);
                    system->advance(phase);
                    start_rsp(*system, dma);
                }
                const u64 start = batched->cpu.cycles;
                compare(*batched, *stepped, 4U);
                CHECK_EQ(batched->cpu.cycles - start, wide ? 69U : 40U);
                CHECK(read_be32(batched->rsp.memory.data() + 0x88U) != 0U);
                if (dma)
                    for (unsigned byte = 0; byte < 32U; ++byte)
                        CHECK_EQ(batched->rsp.memory[0x300U + byte], static_cast<u8>(0x80U + byte));
                compare(*batched, *stepped, 5U);
            }
}

TEST(cpu_cached_arithmetic_keeps_compare_and_audio_callback_boundaries) {
    using Observation = std::array<u64, 6>;
    for (bool wide : {false, true}) {
        auto batched = std::make_unique<System>();
        auto stepped = std::make_unique<System>();
        std::vector<Observation> first, second;
        const auto attach = [&](System& system, std::vector<Observation>& outputs) {
            prepare(system, wide);
            system.bus.set_audio_sample_output([&system, &outputs](const AudioSample& sample) {
                outputs.push_back({sample.rcp_cycle, system.cpu.cycles, system.cpu.instruction_count,
                                   system.cpu.pc, system.cpu.fpu.registers[12], system.rsp.pc});
            });
        };
        attach(*batched, first);
        attach(*stepped, second);
        compare(*batched, *stepped, 512U);
        CHECK(!first.empty());
        CHECK(first == second);

        for (auto* system : {batched.get(), stepped.get()}) {
            system->cpu.cp0[11] = static_cast<u32>(system->cpu.cp0[9] + 3U);
            system->cpu.write_cop0(12, 0x34008001U);
        }
        compare(*batched, *stepped, 4U);
        CHECK((batched->cpu.cp0[13] & 0x8000U) != 0U);
        CHECK((batched->cpu.cp0[12] & 2U) != 0U);
    }
}

TEST(cpu_cached_arithmetic_preflight_is_pure_and_rejects_ambiguous_or_trapping_inputs) {
    struct RestoreEnvironment {
        std::fenv_t saved{};
        RestoreEnvironment() {
            std::fegetenv(&saved);
        }
        ~RestoreEnvironment() {
            std::fesetenv(&saved);
        }
    } environment;
    std::fesetround(FE_DOWNWARD);
    std::feraiseexcept(FE_INEXACT);
    auto system = std::make_unique<System>();
    prepare(*system, false);
    const u32 add = arithmetic(16, 4, 2, 6, 0);
    const auto registers = system->cpu.fpu.registers;
    const u32 control = system->cpu.fpu.control;
    const int flags = std::fetestexcept(FE_ALL_EXCEPT);
    CHECK_EQ(system->cpu.fpu.nontrapping_arithmetic_cycles(add), 3U);
    CHECK(system->cpu.fpu.registers == registers);
    CHECK_EQ(system->cpu.fpu.control, control);
    CHECK_EQ(std::fegetround(), FE_DOWNWARD);
    CHECK_EQ(std::fetestexcept(FE_ALL_EXCEPT), flags);
    system->cpu.fpu.registers[2] = 0x00800001U;
    system->cpu.fpu.registers[4] = 0x00800001U;
    CHECK_EQ(system->cpu.fpu.nontrapping_arithmetic_cycles(arithmetic(16, 4, 2, 6, 2)), 0U);
    system->cpu.fpu.registers[2] = 0;
    system->cpu.fpu.registers[4] = 0;
    CHECK_EQ(system->cpu.fpu.nontrapping_arithmetic_cycles(arithmetic(16, 4, 2, 6, 3)), 0U);
    system->cpu.fpu.control &= ~(1U << 11U);
    CHECK_EQ(system->cpu.fpu.nontrapping_arithmetic_cycles(arithmetic(16, 4, 2, 6, 3)), 2U);
    for (u64 rejected : {1ULL, 0x7fc00000ULL, 0x7f800001ULL}) {
        system->cpu.fpu.registers[2] = rejected;
        CHECK_EQ(system->cpu.fpu.nontrapping_arithmetic_cycles(add), 0U);
    }
}
