#include "../../src/vi/reconstruction.hpp"
#include "cupid/vi/filter.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {
using namespace cupid;

u32 random_word(u32& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}
} // namespace

TEST(vi_edge_reconstruction_matches_sorted_covered_samples_and_signed_rounding) {
    u32 state = 0xc271e491U;
    for (unsigned case_index = 0; case_index < 65536U; ++case_index) {
        ViPixel center{};
        center.coverage = case_index & 7U;
        for (auto& color : center.color)
            color = random_word(state) >> 24U;
        std::array<ViPixel, 6> neighbors{};
        for (auto& neighbor : neighbors) {
            neighbor.coverage = random_word(state) >> 29U;
            for (auto& color : neighbor.color)
                color = random_word(state) >> 24U;
        }
        const auto result = vi::reconstruct_edges(center, neighbors);
        ViColor expected{};
        for (unsigned channel = 0; channel < 3U; ++channel) {
            std::vector<u32> samples{center.color[channel], center.color[channel]};
            for (const auto& neighbor : neighbors)
                if (neighbor.coverage == 7U)
                    samples.push_back(neighbor.color[channel]);
            std::sort(samples.begin(), samples.end());
            const auto difference = static_cast<s32>(samples[1] + samples[samples.size() - 2U]) -
                                    2 * static_cast<s32>(center.color[channel]);
            // Floor division expresses the signed hardware shift independently.
            const s32 numerator = difference * static_cast<s32>(7U - center.coverage) + 4;
            const s32 delta = numerator >= 0 ? numerator / 8 : -((-numerator + 7) / 8);
            expected[channel] = static_cast<u32>(static_cast<s32>(center.color[channel]) + delta) & 255U;
        }
        CHECK_EQ(result, expected);
    }
}

TEST(vi_dither_restoration_matches_neighbor_votes_and_output_clamping) {
    for (unsigned value = 0; value < 256U; ++value) {
        for (unsigned higher = 0; higher <= 8U; ++higher) {
            for (unsigned lower = 0; lower + higher <= 8U; ++lower) {
                const ViColor original{value, 255U - value, (value + 127U) & 255U};
                vi::DitherRestoration restoration(original);
                std::array<s32, 3> votes{};
                for (unsigned index = 0; index < 8U; ++index) {
                    ViPixel neighbor{};
                    for (unsigned channel = 0; channel < 3U; ++channel) {
                        const unsigned quantized = original[channel] / 8U;
                        const unsigned wanted = (index + channel) % 8U;
                        unsigned selected = quantized;
                        if (wanted < higher)
                            selected = std::min(31U, quantized + 1U);
                        else if (wanted < higher + lower)
                            selected = quantized == 0U ? 0U : quantized - 1U;
                        neighbor.color[channel] = selected * 8U + (index & 7U);
                        votes[channel] += selected > quantized ? 1 : selected < quantized ? -1 : 0;
                    }
                    neighbor.coverage = index;
                    restoration.add(neighbor);
                }
                ViColor expected{};
                for (unsigned channel = 0; channel < 3U; ++channel)
                    expected[channel] = static_cast<u32>(std::clamp<s32>(
                        static_cast<s32>((original[channel] / 8U) * 8U) + votes[channel], 0, 255));
                CHECK_EQ(restoration.color(), expected);
            }
        }
    }
}
