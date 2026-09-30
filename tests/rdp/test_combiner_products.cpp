#include "color_commands.hpp"

#include <algorithm>
#include <array>

using namespace cupid;
using namespace test::rdp;

TEST(rdp_combiner_products_preserve_signed_multipliers_rounding_and_color_lanes) {
    RdpColorState state;
    state.combine = combine_word({}, {.a = 1, .b = 7, .c = 15, .d = 2, .aa = 7, .ab = 7, .ac = 7, .ad = 7});
    constexpr std::array<s32, 6> additions{-128, -1, 0, 127, 255, 383};
    for (s32 difference = -511; difference <= 511; ++difference) {
        const s32 a = difference >= 0 ? std::min(difference, 383) : -128;
        const s32 b = a - difference;
        state.convert[4] = static_cast<u16>(static_cast<u32>(b) & 511U);
        for (unsigned multiplier = 0; multiplier < 512U; ++multiplier) {
            state.convert[5] = static_cast<u16>(multiplier);
            const auto plan = rdp_prepare_combiner(state);
            RdpColorInputs inputs;
            inputs.texel0 = {a, a + 128, a - 128, 0};
            for (const s32 addition : additions) {
                inputs.texel1 = {addition, addition + 23, addition - 71, 0};
                const u64 modes = (multiplier & 1U) != 0U ? 1ULL << 13U : 0U;
                const unsigned coverage = multiplier % 9U;
                const unsigned dither = multiplier % 8U;
                const auto actual = rdp_combine_prepared(state, plan, modes, inputs, coverage, dither);
                const auto expected = rdp_combine(state, modes, inputs, coverage, dither);
                CHECK_EQ(actual.color, expected.color);
                CHECK_EQ(actual.coverage, expected.coverage);
                CHECK_EQ(actual.test_alpha, expected.test_alpha);
            }
        }
    }
}
