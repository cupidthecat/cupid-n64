#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

constexpr u32 ViHSync = 0x0440001c;
constexpr u32 RiRefresh = 0x04700010;
constexpr u32 RiBankStatus = 0x0470001c;

void prepare_load(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.gpr[1] = 0xffffffff80100000ULL;
    system.bus.write(0x1000, 4, 0x8c220000); // LW v0, 0(at)
    u64 ignored = 0;
    CHECK(system.cpu.read_memory(system.cpu.pc, 4, ignored, true));
}

u64 line_cycles(const System& system, u32 hsync) {
    return ((static_cast<u64>(hsync) + 1U) * 62500000U + system.video_frequency() - 1U) /
           system.video_frequency();
}

} // namespace

TEST(cpu_rdram_transfer_waits_for_refresh_that_begins_before_the_response) {
    constexpr u32 hsync = 99;
    constexpr u32 recovery = 10;
    for (bool crosses_refresh : {false, true}) {
        System system;
        prepare_load(system);
        system.bus.write(ViHSync, 4, hsync);
        system.bus.write(RiRefresh, 4, 0x20000U | (recovery << 8U) | recovery);
        system.bus.write(RiBankStatus, 4, 0);
        const u64 line = line_cycles(system, hsync);
        system.bus.tick(line - (crosses_refresh ? 10U : 30U));
        CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
        system.cpu.write_cop0(11, 4);
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, crosses_refresh ? 57U : 42U);
        CHECK_EQ(system.cpu.cp0[13] & 0x8000U, 0x8000U);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    }
}

TEST(cpu_rdram_refresh_overlap_uses_the_phase_after_the_nominal_response) {
    struct PhaseCase {
        u64 preamble_cpu_cycles;
        u64 preamble_rcp_cycles;
        u64 expected_step_cycles;
    };

    // The instruction's initial cycle is synchronized before the request. These
    // preambles therefore put the request at RCP fractions 2, 1, and 0. The first
    // request needs the second SClock synchronization cycle, so the refill waits are
    // 41, 40, and 40 cycles and reach fractions 0, 0, and 2. Converting the 10-RCP
    // recovery at those endpoints gives literal totals 57, 56, 55.
    constexpr PhaseCase cases[] = {
        {0, 0, 57},
        {1, 0, 56},
        {2, 1, 55},
    };
    constexpr u32 hsync = 99;
    for (const auto& phase : cases) {
        System system;
        prepare_load(system);
        system.bus.write(ViHSync, 4, hsync);
        system.bus.write(RiRefresh, 4, 0x00020a0aU);
        system.bus.write(RiBankStatus, 4, 0);
        system.advance(phase.preamble_cpu_cycles);
        system.bus.tick(line_cycles(system, hsync) - phase.preamble_rcp_cycles - 10U);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
        system.cpu.step();
        CHECK_EQ(system.cpu.cycles, phase.expected_step_cycles);
        CHECK_EQ(system.bus.rdram_refresh_wait(), 0U);
    }
}

TEST(cpu_rdram_refresh_overlap_is_independent_of_prior_tick_partitioning) {
    constexpr u32 hsync = 99;
    System bulk;
    System single;
    for (System* system : {&bulk, &single}) {
        prepare_load(*system);
        system->bus.write(ViHSync, 4, hsync);
        system->bus.write(RiRefresh, 4, 0x00020a0aU);
        system->bus.write(RiBankStatus, 4, 0);
    }
    const u64 elapsed = line_cycles(bulk, hsync) - 10U;
    bulk.bus.tick(elapsed);
    for (u64 cycle = 0; cycle < elapsed; ++cycle)
        single.bus.tick(1);
    bulk.cpu.step();
    single.cpu.step();
    CHECK_EQ(bulk.cpu.cycles, single.cpu.cycles);
    CHECK_EQ(bulk.cpu.gpr[2], single.cpu.gpr[2]);
    CHECK_EQ(bulk.bus.rdram_refresh_wait(), single.bus.rdram_refresh_wait());
}
