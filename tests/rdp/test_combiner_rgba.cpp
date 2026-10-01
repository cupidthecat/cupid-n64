#include "color_commands.hpp"

#include <array>

using namespace cupid;
using namespace test::rdp;

namespace {

void equal_pixel(const RdpCombinedPixel& actual, const RdpCombinedPixel& expected) {
    CHECK_EQ(actual.color, expected.color);
    CHECK_EQ(actual.coverage, expected.coverage);
    CHECK_EQ(actual.test_alpha, expected.test_alpha);
}

u32 next_random(u32& seed) {
    seed ^= seed << 13U;
    seed ^= seed >> 17U;
    seed ^= seed << 5U;
    return seed;
}

} // namespace

TEST(rdp_combiner_shared_rgba_products_and_direct_terms_match_scalar_cycles) {
    constexpr std::array<CombineCycle, 4> cycles{
        CombineCycle{.a = 1, .b = 2, .c = 4, .d = 5, .aa = 1, .ab = 2, .ac = 4, .ad = 5},
        CombineCycle{.a = 4, .b = 5, .c = 3, .d = 2, .aa = 4, .ab = 5, .ac = 3, .ad = 2},
        CombineCycle{.a = 3, .b = 5, .c = 1, .d = 4, .aa = 3, .ab = 5, .ac = 1, .ad = 4},
        CombineCycle{.a = 4, .b = 1, .c = 2, .d = 3, .aa = 4, .ab = 1, .ac = 2, .ad = 3}};
    constexpr std::array<s32, 16> edges{-513, -512, -257, -256, -129, -128, -1,  0,
                                        1,    127,  128,  255,  256,  383,  511, 512};
    u32 seed = 0x38e4a067U;
    unsigned compared = 0;
    for (unsigned iteration = 0; iteration < 4096U; ++iteration) {
        RdpColorInputs inputs;
        for (auto* color : {&inputs.texel0, &inputs.texel1, &inputs.shade})
            for (auto& channel : *color)
                channel = iteration < edges.size() ? edges[(iteration + next_random(seed)) % edges.size()]
                                                   : static_cast<s32>(next_random(seed) & 1023U) - 512;
        inputs.lod_fraction = static_cast<s32>(next_random(seed) & 255U);
        inputs.noise = {static_cast<s32>(next_random(seed) & 255U),
                        static_cast<s32>(next_random(seed) & 255U)};
        RdpColorState state;
        state.primitive = (next_random(seed) & 0xfeffff00U) | 0x01000061U;
        state.environment = (next_random(seed) & 0xfeffff00U) | 0xd3U;
        state.primitive_lod = static_cast<u8>(next_random(seed));
        state.key_width = {32, 64, 128};
        state.key_center = {17, 89, 157};
        state.key_scale = {37, 193, 255};
        const unsigned coverage = iteration % 9U;
        const unsigned dither = iteration % 8U;
        for (unsigned equation = 0; equation < cycles.size(); ++equation) {
            for (unsigned variant = 0; variant < 4U; ++variant) {
                auto last = cycles[equation];
                if (variant == 1U) {
                    last.ad = (last.ad + 1U) % 6U;
                } else if (variant >= 2U) {
                    last.b = last.a;
                    last.ab = last.aa;
                    if (variant == 3U)
                        last.ad = (last.ad + 1U) % 6U;
                }
                state.combine = combine_word(cycles[(equation + 1U) % cycles.size()], last);
                const auto plan = rdp_prepare_combiner(state);
                CHECK(plan.rgba_product[0]);
                if (variant == 0U)
                    CHECK(plan.rgba_product[1]);
                else if (variant == 1U)
                    CHECK(!plan.rgba_product[1]);
                else if (variant == 2U)
                    CHECK(plan.rgba_direct[1]);
                else
                    CHECK(!plan.rgba_direct[1]);
                for (unsigned mode = 0; mode < 4U; ++mode) {
                    const u64 modes = (static_cast<u64>(mode & 1U) << 52U) |
                                      (static_cast<u64>((mode >> 1U) & 1U) << 40U) |
                                      (static_cast<u64>(iteration & 1U) << 12U) |
                                      (static_cast<u64>((iteration >> 1U) & 1U) << 13U);
                    equal_pixel(rdp_combine_prepared(state, plan, modes, inputs, coverage, dither),
                                rdp_combine(state, modes, inputs, coverage, dither));
                    ++compared;
                }
            }
        }
    }
    CHECK_EQ(compared, 262144U);
}
