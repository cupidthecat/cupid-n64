#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <limits>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;

void prepare(System& system, u64 address = code, u32 delay_slot = 0, bool jump = false) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    const auto physical = static_cast<u32>(address) & 0x1fffffffU;
    const u32 branch = jump ? 0x08000000U | ((physical >> 2) & 0x03ffffffU) : 0x1000ffffU;
    system.bus.write(physical, 4, branch);
    system.bus.write(physical + 4, 4, delay_slot);
    system.cpu.set_pc(address);
}

unsigned stepped_slice(System& system, unsigned maximum_steps, u64 maximum_cycles) {
    const auto start = system.cpu.cycles;
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
    CHECK(batched.bus.rdram == stepped.bus.rdram);
    for (u32 address : {0x04300008U, 0x04400010U, 0x04500004U, 0x0450000cU, 0x04600010U, 0x04800018U,
                        0x0410000cU, 0x04100010U})
        CHECK_EQ(batched.bus.read(address, 4), stepped.bus.read(address, 4));
    for (unsigned index = 0; index < batched.cpu.instruction_cache.size(); ++index) {
        const auto& a = batched.cpu.instruction_cache[index];
        const auto& b = stepped.cpu.instruction_cache[index];
        CHECK(a.data == b.data);
        CHECK_EQ(a.tag, b.tag);
        CHECK_EQ(a.valid, b.valid);
    }
}

void compare_slice(System& batched, System& stepped, unsigned steps, u64 cycles = 1000000) {
    const auto expected = stepped_slice(stepped, steps, cycles);
    CHECK_EQ(batched.cpu.run_slice(steps, cycles), expected);
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

using Observation = std::array<u64, 4>;
Observation observe(System& system) {
    return {system.cpu.cycles, system.cpu.instruction_count, system.cpu.pc, system.cpu.read_cop0(9)};
}

} // namespace

TEST(cpu_idle_slice_matches_cold_and_warm_fetches_at_each_system_phase) {
    for (const u64 offset : {0ULL, 28ULL}) {
        for (unsigned phase = 0; phase < 3; ++phase) {
            for (const bool jump : {false, true}) {
                System batched, stepped;
                prepare(batched, code + offset, 0, jump);
                prepare(stepped, code + offset, 0, jump);
                batched.advance(phase);
                stepped.advance(phase);
                compare_slice(batched, stepped, 10001);
                CHECK(batched.cpu.batched_idle_instructions() > 9000);
                compare_slice(batched, stepped, 37);
            }
        }
    }
}

TEST(cpu_idle_slice_preserves_random_for_every_wired_range) {
    for (unsigned wired = 0; wired < 64; ++wired) {
        System batched, stepped;
        prepare(batched);
        prepare(stepped);
        batched.cpu.write_cop0(6, wired);
        stepped.cpu.write_cop0(6, wired);
        compare_slice(batched, stepped, 331);
        CHECK(batched.cpu.batched_idle_instructions() != 0);
        compare_slice(batched, stepped, 129);
    }
}

TEST(cpu_idle_slice_obeys_small_instruction_and_cycle_budgets) {
    for (unsigned budget = 0; budget < 17; ++budget) {
        System batched, stepped;
        prepare(batched);
        prepare(stepped);
        compare_slice(batched, stepped, 20);
        compare_slice(batched, stepped, budget);
        compare_slice(batched, stepped, 100, budget);
        compare_slice(batched, stepped, 23);
    }
}

TEST(cpu_idle_slice_preserves_count_compare_edges_and_delay_slot_exceptions) {
    for (unsigned phase = 0; phase < 2; ++phase) {
        for (unsigned distance = 1; distance < 9; ++distance) {
            System batched, stepped;
            prepare(batched);
            prepare(stepped);
            compare_slice(batched, stepped, 4 + phase);
            const u32 compare = static_cast<u32>(stepped.cpu.read_cop0(9)) + distance;
            for (auto* system : {&batched, &stepped}) {
                system->cpu.write_cop0(11, compare);
                system->cpu.write_cop0(12, 0x34008001);
            }
            compare_slice(batched, stepped, 40);
            CHECK_EQ(batched.cpu.cp0[13] & 0x8000U, 0x8000U);
            CHECK_EQ(batched.cpu.cp0[14], code);
        }
    }
}

TEST(cpu_idle_slice_preserves_count_wrap_and_does_not_batch_uncached_or_mutating_loops) {
    for (const u64 address : {code, u64{0xffffffffa0001000ULL}}) {
        for (const u32 delay : {0U, 0x24420001U}) { // ADDIU v0,v0,1
            System batched, stepped;
            prepare(batched, address, delay);
            prepare(stepped, address, delay);
            for (auto* system : {&batched, &stepped}) {
                system->cpu.write_cop0(9, 0xfffffff0U);
                system->cpu.write_cop0(11, 0);
            }
            compare_slice(batched, stepped, 301);
            if (address != code || delay != 0)
                CHECK_EQ(batched.cpu.batched_idle_instructions(), 0U);
            CHECK_EQ(batched.cpu.cp0[13] & 0x8000U, 0x8000U);
        }
    }
}

TEST(cpu_idle_slice_delivers_video_callbacks_at_identical_cpu_state) {
    for (const bool redirect : {false, true}) {
        System batched, stepped;
        std::vector<Observation> a, b;
        const auto attach = [&](System& system, std::vector<Observation>& observations) {
            prepare(system);
            short_video(system);
            system.bus.set_video_output([&, machine = &system, output = &observations](VideoField) {
                output->push_back(observe(*machine));
                if (redirect && output->size() == 1)
                    machine->cpu.set_pc(code + 0x100);
            });
        };
        attach(batched, a);
        attach(stepped, b);
        compare_slice(batched, stepped, 4096);
        CHECK(!a.empty());
        CHECK(a == b);
        CHECK(batched.cpu.batched_idle_instructions() != 0);
    }
}

TEST(cpu_idle_slice_accepts_vi_interrupts_without_video_output) {
    System batched, stepped;
    for (auto* system : {&batched, &stepped}) {
        prepare(*system);
        short_video(*system);
        system->bus.write(0x0430000c, 4, 0x80);
        system->cpu.write_cop0(12, 0x34000401);
    }
    compare_slice(batched, stepped, 4096);
    CHECK_EQ(batched.cpu.cp0[13] & 0x400U, 0x400U);
    CHECK(batched.cpu.batched_idle_instructions() != 0);
}

TEST(cpu_idle_slice_preserves_audio_edges_and_callback_nmi) {
    System batched, stepped;
    std::vector<Observation> a, b;
    const auto attach = [](System& system, std::vector<Observation>& observations) {
        prepare(system);
        system.bus.write(0x04500010, 4, 99);
        system.bus.set_audio_sample_output([machine = &system, output = &observations](const AudioSample&) {
            output->push_back(observe(*machine));
            if (output->size() == 1)
                machine->cpu.request_nmi();
        });
    };
    attach(batched, a);
    attach(stepped, b);
    compare_slice(batched, stepped, 1000);
    CHECK(!a.empty());
    CHECK(a == b);
    CHECK(batched.cpu.batched_idle_instructions() != 0);
}

TEST(cpu_idle_slice_preserves_dma_payloads_and_refresh_progress) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system);
            short_video(*system);
            system->bus.write(0x04700010, 4, 0x20a0a);
            system->bus.write(0x0470001c, 4, 0);
            system->bus.rom.assign(4096, 0x5a);
            system->bus.write(0x04600000, 4, 0x4000);
            system->bus.write(0x04600004, 4, 0x10000000);
            system->bus.write(0x0460000c, 4, 127);
            system->bus.write(0x2000, 4, 0x12345678);
            system->rsp.write_register(0, 0);
            system->rsp.write_register(4, 0x2000);
            system->rsp.write_register(8, 15);
            system->advance(phase);
        }
        compare_slice(batched, stepped, 4096);
        CHECK_EQ(read_be32(batched.rsp.memory.data()), 0x12345678U);
        CHECK_EQ(batched.bus.read_ram_byte(0x4000), 0x5aU);
        CHECK(batched.cpu.batched_idle_instructions() != 0);
    }
}

TEST(cpu_idle_slice_steps_active_rsp_and_deferred_cpu_writes) {
    for (const bool active_rsp : {false, true}) {
        System batched, stepped;
        for (auto* system : {&batched, &stepped}) {
            prepare(*system);
            if (active_rsp) {
                write_be32(system->rsp.memory.data() + 0x1000, 0x1000ffff);
                system->rsp.write_register(0x10, 1);
            } else {
                system->bus.write(0x0ffc, 4, 0xac220000); // SW v0,0(at)
                system->cpu.gpr[1] = 0xffffffffa0002000ULL;
                system->cpu.gpr[2] = 0x12345678;
                system->cpu.set_pc(code - 4);
            }
        }
        compare_slice(batched, stepped, 4096);
        if (active_rsp)
            CHECK_EQ(batched.cpu.batched_idle_instructions(), 0U);
        else {
            CHECK_EQ(batched.bus.read_ram_byte(0x2000), 0x12U);
            CHECK(batched.cpu.batched_idle_instructions() != 0);
        }
    }
}
