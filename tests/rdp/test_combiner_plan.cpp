#include "color_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {

void check_equal(const RdpCombinedPixel& actual, const RdpCombinedPixel& expected) {
    CHECK_EQ(actual.color, expected.color);
    CHECK_EQ(actual.coverage, expected.coverage);
    CHECK_EQ(actual.test_alpha, expected.test_alpha);
}

CombineCycle zero_cycle() {
    return {.a = 8, .b = 8, .c = 16, .d = 7, .aa = 7, .ab = 7, .ac = 7, .ad = 7};
}

void set_rgb_selector(CombineCycle& cycle, unsigned term, unsigned selector) {
    switch (term) {
    case 0:
        cycle.a = selector;
        break;
    case 1:
        cycle.b = selector;
        break;
    case 2:
        cycle.c = selector;
        break;
    default:
        cycle.d = selector;
        break;
    }
}

void set_alpha_selector(CombineCycle& cycle, unsigned term, unsigned selector) {
    switch (term) {
    case 0:
        cycle.aa = selector;
        break;
    case 1:
        cycle.ab = selector;
        break;
    case 2:
        cycle.ac = selector;
        break;
    default:
        cycle.ad = selector;
        break;
    }
}

RdpColorState selector_state() {
    RdpColorState state;
    state.primitive = 0xb49678a0U;
    state.environment = 0x3c465060U;
    state.primitive_lod = 144;
    state.key_width = {64, 80, 96};
    state.key_center = {20, 40, 60};
    state.key_scale = {64, 96, 128};
    state.convert[4] = 32;
    state.convert[5] = 80;
    return state;
}

RdpColorInputs selector_inputs() {
    RdpColorInputs inputs;
    inputs.texel0 = {30, 50, 70, 90};
    inputs.texel1 = {100, 110, 120, 130};
    inputs.shade = {140, 130, 110, 150};
    inputs.lod_fraction = 72;
    inputs.noise = {44, 88};
    return inputs;
}

RdpCombinedPixel check_plan(RdpColorState state, u64 modes, const RdpColorInputs& inputs,
                            unsigned coverage = 5, unsigned dither = 3) {
    const auto plan = rdp_prepare_combiner(state);
    const auto expected = rdp_combine(state, modes, inputs, coverage, dither);
    check_equal(rdp_combine_prepared(state, plan, modes, inputs, coverage, dither), expected);
    return expected;
}

bool same_pixel(const RdpCombinedPixel& left, const RdpCombinedPixel& right) {
    return left.color == right.color && left.coverage == right.coverage &&
           left.test_alpha == right.test_alpha;
}

CombineCycle probe_cycle() {
    return {.a = 3, .b = 5, .c = 6, .d = 4, .aa = 3, .ab = 5, .ac = 6, .ad = 4};
}

CombineCycle pass_combined_cycle() {
    auto cycle = zero_cycle();
    cycle.d = 0;
    cycle.ad = 0;
    return cycle;
}

CombineCycle seed_combined_cycle() {
    auto cycle = zero_cycle();
    cycle.d = 3;
    cycle.ad = 3;
    return cycle;
}

unsigned zero_rgb_selector(unsigned term) {
    if (term == 2U)
        return 16;
    return term == 3U ? 7U : 8U;
}

u32 packed_rgb(const RdpCombinedPixel& pixel) {
    return (static_cast<u32>(pixel.color[0]) << 24U) | (static_cast<u32>(pixel.color[1]) << 16U) |
           (static_cast<u32>(pixel.color[2]) << 8U) | 0xe0U;
}

constexpr u64 default_modes = 15ULL << 36U;

} // namespace

TEST(rdp_combiner_plan_matches_scalar_for_every_raw_rgb_selector) {
    const auto inputs = selector_inputs();
    constexpr unsigned selector_counts[4] = {16, 16, 32, 8};
    std::array<std::array<bool, 32>, 4> observed{};
    for (unsigned target_cycle = 0; target_cycle < 2; ++target_cycle) {
        for (unsigned term = 0; term < 4; ++term) {
            for (unsigned selector = 0; selector < selector_counts[term]; ++selector) {
                for (const bool two_cycles : {false, true}) {
                    if (target_cycle == 0U && !two_cycles)
                        continue;
                    for (const bool key_enabled : {false, true}) {
                        auto state = selector_state();
                        auto first = target_cycle == 0U ? probe_cycle()
                                                        : (two_cycles ? seed_combined_cycle() : zero_cycle());
                        auto second = target_cycle == 0U ? pass_combined_cycle() : probe_cycle();
                        set_rgb_selector(target_cycle == 0U ? first : second, term, selector);
                        state.combine = combine_word(first, second);
                        const u64 modes = (two_cycles ? 1ULL << 52U : 0U) | (key_enabled ? 1ULL << 40U : 0U);
                        const auto expected = check_plan(state, modes, inputs);

                        if ((term == 1U || term == 2U) && !key_enabled) {
                            set_rgb_selector(target_cycle == 0U ? first : second, term,
                                             zero_rgb_selector(term));
                            state.combine = combine_word(first, second);
                            const auto baseline = rdp_combine(state, modes, inputs, 5, 3);
                            observed[term][selector] =
                                observed[term][selector] || !same_pixel(expected, baseline);
                        }
                    }
                }
            }
        }
    }
    for (unsigned selector = 0; selector < 8; ++selector)
        CHECK(observed[1][selector]);
    for (unsigned selector = 0; selector < 16; ++selector)
        CHECK(observed[2][selector]);
}

TEST(rdp_combiner_plan_matches_scalar_for_every_raw_alpha_selector) {
    const auto inputs = selector_inputs();
    std::array<std::array<bool, 8>, 4> observed{};
    for (unsigned target_cycle = 0; target_cycle < 2; ++target_cycle) {
        for (unsigned term = 0; term < 4; ++term) {
            for (unsigned selector = 0; selector < 8; ++selector) {
                for (const bool two_cycles : {false, true}) {
                    if (target_cycle == 0U && !two_cycles)
                        continue;
                    for (const bool key_enabled : {false, true}) {
                        auto state = selector_state();
                        auto first = target_cycle == 0U ? probe_cycle()
                                                        : (two_cycles ? seed_combined_cycle() : zero_cycle());
                        auto second = target_cycle == 0U ? pass_combined_cycle() : probe_cycle();
                        set_alpha_selector(target_cycle == 0U ? first : second, term, selector);
                        state.combine = combine_word(first, second);
                        const u64 modes = (two_cycles ? 1ULL << 52U : 0U) | (key_enabled ? 1ULL << 40U : 0U) |
                                          (1ULL << 12U) | (1ULL << 13U);
                        const auto expected = check_plan(state, modes, inputs);

                        if ((term == 1U || term == 2U) && !key_enabled) {
                            set_alpha_selector(target_cycle == 0U ? first : second, term, 7);
                            state.combine = combine_word(first, second);
                            const auto baseline = rdp_combine(state, modes, inputs, 5, 3);
                            observed[term][selector] =
                                observed[term][selector] || !same_pixel(expected, baseline);
                        }
                    }
                }
            }
        }
    }
    for (unsigned term : {1U, 2U})
        for (unsigned selector = 0; selector < 7; ++selector)
            CHECK(observed[term][selector]);
}

TEST(rdp_combiner_plan_randomized_matches_scalar_across_modes_coverage_and_dither) {
    u32 random = 0x6d2b79f5U;
    const auto next = [&] {
        random = random * 1664525U + 1013904223U;
        return random;
    };
    const auto component = [&] { return static_cast<s32>(next() & 511U) - 128; };
    const auto color = [&] {
        RdpColor result;
        for (auto& value : result)
            value = component();
        return result;
    };

    for (unsigned coverage = 0; coverage <= 8; ++coverage) {
        for (unsigned dither = 0; dither < 8; ++dither) {
            for (unsigned sample = 0; sample < 4; ++sample) {
                RdpColorState state;
                state.combine = ((static_cast<u64>(next()) << 32U) | next()) & 0x00ffffffffffffffULL;
                state.primitive = next();
                state.environment = next();
                state.primitive_lod = static_cast<u8>(next());
                for (unsigned channel = 0; channel < 3; ++channel) {
                    state.key_width[channel] = static_cast<u16>(next() & 4095U);
                    state.key_center[channel] = static_cast<u8>(next());
                    state.key_scale[channel] = static_cast<u8>(next());
                }
                for (auto& value : state.convert)
                    value = static_cast<u16>(next() & 511U);

                RdpColorInputs inputs;
                inputs.texel0 = color();
                inputs.texel1 = color();
                inputs.shade = color();
                inputs.lod_fraction = component();
                inputs.noise = {static_cast<s32>(next() & 255U), static_cast<s32>(next() & 255U)};

                const bool coverage_times_alpha = (next() & 1U) != 0;
                const bool alpha_coverage_select = (next() & 1U) != 0;
                for (const bool two_cycles : {false, true}) {
                    for (const bool key_enabled : {false, true}) {
                        const u64 modes = (two_cycles ? 1ULL << 52U : 0U) | (key_enabled ? 1ULL << 40U : 0U) |
                                          (coverage_times_alpha ? 1ULL << 12U : 0U) |
                                          (alpha_coverage_select ? 1ULL << 13U : 0U);
                        check_plan(state, modes, inputs, coverage, dither);
                    }
                }
            }
        }
    }
}

TEST(rdp_combiner_plan_refreshes_key_constants_without_setcombine) {
    ColorCommands commands;
    constexpr u32 primitive = 0xc09060ffU;
    const auto combine = combine_word({}, {.a = 3, .b = 6, .c = 6, .d = 7});
    commands.append(0x3a, primitive);
    commands.append(0x2b, (16ULL << 16U) | (48ULL << 8U) | 224U);
    commands.append(0x2a, (16ULL << 44U) | (16ULL << 32U) | (80ULL << 24U) | (192ULL << 16U) |
                              (112ULL << 8U) | 160U);
    commands.append(0x3c, combine);
    commands.rectangle();
    commands.append(0x2b, (16ULL << 16U) | (144ULL << 8U) | 96U);
    commands.append(0x2a,
                    (16ULL << 44U) | (16ULL << 32U) | (32ULL << 24U) | (64ULL << 16U) | (208ULL << 8U) | 32U);
    commands.rectangle(4, 0, 8, 4);
    commands.run();

    RdpColorState first;
    first.primitive = primitive;
    first.combine = combine;
    first.key_center = {48, 80, 112};
    first.key_scale = {224, 192, 160};
    auto second = first;
    second.key_center = {144, 32, 208};
    second.key_scale = {96, 64, 32};
    CHECK_EQ(commands.pixel(0), packed_rgb(rdp_combine(first, default_modes, {}, 8, 0)));
    CHECK_EQ(commands.pixel(1), packed_rgb(rdp_combine(second, default_modes, {}, 8, 0)));
}

TEST(rdp_combiner_plan_refreshes_convert_constants_without_setcombine) {
    ColorCommands commands;
    const auto combine = combine_word({}, {.a = 8, .b = 7, .c = 15, .d = 7});
    commands.append(0x2c, (0x1f3ULL << 9U) | 0x12dU);
    commands.append(0x3c, combine);
    commands.rectangle();
    commands.append(0x2c, (0x087ULL << 9U) | 0x1b4U);
    commands.rectangle(4, 0, 8, 4);
    commands.run();

    RdpColorState first;
    first.combine = combine;
    first.convert[4] = 0x1f3;
    first.convert[5] = 0x12d;
    auto second = first;
    second.convert[4] = 0x087;
    second.convert[5] = 0x1b4;
    CHECK_EQ(commands.pixel(0), packed_rgb(rdp_combine(first, default_modes, {}, 8, 0)));
    CHECK_EQ(commands.pixel(1), packed_rgb(rdp_combine(second, default_modes, {}, 8, 0)));
}

TEST(rdp_combiner_plan_refreshes_primitive_lod_without_setcombine) {
    ColorCommands commands;
    const auto combine = combine_word({}, {.a = 6, .b = 7, .c = 14, .d = 7});
    commands.append(0x2c, 0);
    commands.append(0x3c, combine);
    commands.append(0x3a, (37ULL << 32U) | 0x804020ffU);
    commands.rectangle();
    commands.append(0x3a, (201ULL << 32U) | 0x804020ffU);
    commands.rectangle(4, 0, 8, 4);
    commands.run();

    RdpColorState first;
    first.combine = combine;
    first.primitive = 0x804020ffU;
    first.primitive_lod = 37;
    auto second = first;
    second.primitive_lod = 201;
    CHECK_EQ(commands.pixel(0), packed_rgb(rdp_combine(first, default_modes, {}, 8, 0)));
    CHECK_EQ(commands.pixel(1), packed_rgb(rdp_combine(second, default_modes, {}, 8, 0)));
}

TEST(rdp_combiner_plan_refreshes_mux_at_each_draw) {
    ColorCommands commands;
    commands.append(0x3a, 0x123456ffU);
    commands.append(0x3b, 0xabcdef80U);
    commands.append(0x3c, combine_word({}, {.d = 3, .ad = 3}));
    commands.rectangle();
    commands.append(0x3c, combine_word({}, {.d = 5, .ad = 5}));
    commands.rectangle(4, 0, 8, 4);
    commands.run();
    CHECK_EQ(commands.pixel(0), 0x123456e0U);
    CHECK_EQ(commands.pixel(1), 0xabcdefe0U);
}

TEST(rdp_combiner_plan_rebuilds_from_reset_state_without_setcombine) {
    ColorCommands commands;
    commands.append(0x3b, 0xff0000ffU);
    commands.append(0x3c, combine_word({}, {.d = 5, .ad = 5}));
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0xff0000e0U);

    commands.system->bus.rdp.reset();
    commands.end = 0x1000;
    commands.append(0x3f, (3ULL << 51U) | (7ULL << 32U) | 0x8000U);
    commands.modes();
    commands.rectangle();
    commands.run();
    CHECK_EQ(commands.pixel(), 0x000000e0U);
}
