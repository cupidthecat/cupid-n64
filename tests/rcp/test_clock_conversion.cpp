#include "cupid/rcp/clocks.hpp"
#include "test.hpp"

#include <array>
#include <limits>

namespace {
using namespace cupid;
}

TEST(rcp_clock_conversion_empty_wait_has_no_cost_at_any_phase) {
    for (u64 phase = 0; phase < 3; ++phase)
        CHECK_EQ(rcp::cpu_cycles_for_rcp(0, phase), 0U);
}

TEST(rcp_clock_conversion_rounds_up_to_the_first_reachable_edge) {
    constexpr std::array<std::array<u64, 7>, 3> expected{{
        {0, 2, 3, 5, 6, 8, 9},
        {0, 1, 3, 4, 6, 7, 9},
        {0, 1, 2, 4, 5, 7, 8},
    }};
    for (u64 phase = 0; phase < expected.size(); ++phase) {
        for (u64 clocks = 0; clocks < expected[phase].size(); ++clocks)
            CHECK_EQ(rcp::cpu_cycles_for_rcp(clocks, phase), expected[phase][clocks]);
        for (u64 clocks = 1; clocks < 1024; ++clocks) {
            const u64 converted = rcp::cpu_cycles_for_rcp(clocks, phase);
            CHECK_EQ((converted * 2 + phase) / 3, clocks);
            CHECK(((converted - 1) * 2 + phase) / 3 < clocks);
        }
    }
}

TEST(rcp_clock_conversion_preserves_large_representable_waits) {
    constexpr u64 clocks = 6148914691236517206ULL;
    CHECK_EQ(rcp::cpu_cycles_for_rcp(clocks, 0), 9223372036854775809ULL);
    CHECK_EQ(rcp::cpu_cycles_for_rcp(clocks, 1), 9223372036854775809ULL);
    CHECK_EQ(rcp::cpu_cycles_for_rcp(clocks, 2), 9223372036854775808ULL);
}

TEST(rcp_clock_conversion_saturates_beyond_the_cpu_clock_horizon) {
    constexpr u64 maximum = std::numeric_limits<u64>::max();
    constexpr u64 last_representable = 12297829382473034410ULL;
    CHECK_EQ(rcp::cpu_cycles_for_rcp(last_representable, 0), maximum);
    CHECK_EQ(rcp::cpu_cycles_for_rcp(last_representable, 1), maximum);
    CHECK_EQ(rcp::cpu_cycles_for_rcp(last_representable, 2), maximum - 1);
    for (u64 phase = 0; phase < 3; ++phase) {
        CHECK_EQ(rcp::cpu_cycles_for_rcp(last_representable + 1, phase), maximum);
        CHECK_EQ(rcp::cpu_cycles_for_rcp(maximum, phase), maximum);
    }
}
