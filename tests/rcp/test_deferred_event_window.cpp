#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <initializer_list>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;
constexpr u64 reset_delay = 31'250'000ULL;

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

void warm_instruction_cache_line(System& system) {
    const u32 physical = static_cast<u32>(code) & 0x1fffffffU;
    auto& line = system.cpu.instruction_cache[(code >> 5) & 511U];
    line = {};
    line.valid = true;
    line.tag = physical & 0xfffff000U;
    for (unsigned byte = 0; byte < line.data.size(); ++byte)
        line.data[byte] = system.bus.rdram[(physical & ~31U) + byte];
}

void prepare_cached(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    write_program(system, {
                              0U,
                              immediate(0x09, 8, 8, 1),
                              immediate(0x0e, 8, 9, 0x55),
                              immediate(0x05, 8, 0, -3),
                              immediate(0x0d, 10, 10, 1),
                          });
    system.cpu.gpr[8] = 1;
    system.cpu.set_pc(code);
    warm_instruction_cache_line(system);
}

void start_cached(System& system, unsigned phase) {
    system.advance(phase);
    system.cpu.step();
    system.advance(0);
}

void make_cached_lag(System& deferred, System& eager) {
    for (unsigned index = 0; index < 2; ++index) {
        deferred.cpu.step();
        eager.cpu.step();
        eager.advance(0);
    }
}

unsigned eager_slice(System& system, unsigned maximum_steps, u64 maximum_cycles) {
    const u64 start = system.cpu.cycles;
    unsigned steps = 0;
    while (steps < maximum_steps && system.cpu.cycles - start < maximum_cycles && !system.cpu.frozen) {
        system.cpu.step();
        system.advance(0);
        ++steps;
    }
    return steps;
}

void equivalent(System& deferred, System& eager) {
    deferred.settle();
    eager.settle();
    CHECK_EQ(deferred.cpu.cycles, eager.cpu.cycles);
    CHECK_EQ(deferred.cpu.instruction_count, eager.cpu.instruction_count);
    CHECK_EQ(deferred.cpu.pc, eager.cpu.pc);
    CHECK_EQ(deferred.cpu.next_pc, eager.cpu.next_pc);
    CHECK_EQ(deferred.cpu.exception_pending, eager.cpu.exception_pending);
    CHECK_EQ(deferred.cpu.frozen, eager.cpu.frozen);
    CHECK(deferred.cpu.gpr == eager.cpu.gpr);
    CHECK(deferred.cpu.cp0 == eager.cpu.cp0);
    CHECK_EQ(deferred.bus.output_clock(), eager.bus.output_clock());
    CHECK_EQ(deferred.rsp.pc, eager.rsp.pc);
    CHECK(deferred.rsp.memory == eager.rsp.memory);
}

void prepare_idle(System& system, unsigned phase) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    system.bus.write(0x1000, 4, 0x1000ffffU);
    system.bus.write(0x1004, 4, 0U);
    system.cpu.set_pc(code);
    system.advance(phase);
    system.cpu.step();
    system.cpu.step();
    system.advance(0);
    CHECK_EQ(system.cpu.pc, code);
}

void make_idle_lag(System& deferred, System& eager) {
    for (unsigned index = 0; index < 2; ++index) {
        deferred.cpu.step();
        eager.cpu.step();
        eager.advance(0);
    }
    CHECK_EQ(deferred.cpu.pc, code);
    CHECK_EQ(eager.cpu.pc, code);
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

void pif_command(System& system, u8 value) {
    system.bus.write(0x1fc007fc, 4, value);
    system.bus.tick(50'000);
}

void boot_pif(System& system) {
    pif_command(system, 0x10);
    for (unsigned index = 0; index < 6; ++index)
        system.bus.pif[0x7f2 + index] = static_cast<u8>(system.bus.cic.checksum() >> ((5 - index) * 8));
    pif_command(system, 0x20);
    pif_command(system, 0x40);
    pif_command(system, 8);
}

} // namespace

TEST(rcp_deferred_event_window_matches_eager_cached_execution_for_every_phase) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (const u64 budget : {4ULL, 5ULL, 17ULL, 64ULL}) {
            System deferred, eager;
            prepare_cached(deferred);
            prepare_cached(eager);
            start_cached(deferred, phase);
            start_cached(eager, phase);
            make_cached_lag(deferred, eager);

            const u64 previously_batched = deferred.cpu.batched_cached_instructions();
            const unsigned expected = eager_slice(eager, 64, budget);
            CHECK_EQ(deferred.cpu.run_slice(64, budget), expected);
            CHECK(deferred.cpu.batched_cached_instructions() > previously_batched);
            equivalent(deferred, eager);
        }
    }
}

TEST(rcp_deferred_event_window_matches_eager_idle_execution_for_every_phase) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System deferred, eager;
        prepare_idle(deferred, phase);
        prepare_idle(eager, phase);
        make_idle_lag(deferred, eager);

        const u64 previously_batched = deferred.cpu.batched_idle_instructions();
        const unsigned expected = eager_slice(eager, 257, 257);
        CHECK_EQ(deferred.cpu.run_slice(257, 257), expected);
        CHECK(deferred.cpu.batched_idle_instructions() > previously_batched);
        equivalent(deferred, eager);
    }
}

TEST(rcp_deferred_event_window_stops_before_callback_mutation) {
    using Observation = std::array<u64, 4>;
    System deferred, eager;
    std::vector<Observation> first, second;
    const auto prepare = [](System& system, std::vector<Observation>& observations) {
        prepare_cached(system);
        short_video(system);
        system.bus.set_video_output([machine = &system, output = &observations](VideoField) {
            output->push_back({machine->cpu.cycles, machine->cpu.instruction_count, machine->cpu.pc,
                               machine->cpu.read_cop0(9)});
            machine->cpu.request_nmi();
        });
        start_cached(system, 0);
    };
    prepare(deferred, first);
    prepare(eager, second);
    make_cached_lag(deferred, eager);

    const unsigned expected = eager_slice(eager, 4096, 4096);
    CHECK_EQ(deferred.cpu.run_slice(4096, 4096), expected);
    CHECK(!first.empty());
    CHECK(first == second);
    equivalent(deferred, eager);
}

TEST(rcp_reset_release_invalidates_a_reusable_deferred_deadline) {
    System deferred, eager;
    for (auto* system : {&deferred, &eager}) {
        test::initialize_memory(*system);
        boot_pif(*system);
        system->cpu.write_cop0(12, 0x34000000U);
        write_program(*system, {
                                   immediate(0x09, 8, 8, 1),
                                   immediate(0x0e, 8, 9, 0x55),
                                   immediate(0x0d, 10, 10, 1),
                                   immediate(0x05, 8, 0, -4),
                                   0U,
                               });
        system->cpu.gpr[8] = 1;
        system->cpu.set_pc(code);
        warm_instruction_cache_line(*system);
        system->set_reset_button(true);
        system->bus.tick(reset_delay);
        CHECK(system->bus.pif_boot.pre_nmi());
        system->advance(0);
        system->set_reset_button(false);
    }

    for (unsigned index = 0; index < 2; ++index) {
        deferred.cpu.step();
        eager.cpu.step();
        eager.advance(0);
    }
    CHECK_EQ(deferred.cpu.run_slice(1, 2), eager_slice(eager, 1, 2));
    equivalent(deferred, eager);
    CHECK_EQ(deferred.cpu.pc, 0xffffffffbfc00000ULL);
}
