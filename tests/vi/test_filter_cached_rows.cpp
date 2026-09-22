#include "cupid/vi/filter.hpp"
#include "test.hpp"

#include <array>
#include <limits>
#include <vector>

namespace {
using namespace cupid;

void fill_source(std::vector<u8>& bytes, std::vector<u8>& hidden) {
    u32 state = 0x6d2b79f5U;
    for (auto& byte : bytes) {
        state = state * 1664525U + 1013904223U;
        byte = static_cast<u8>(state >> 24U);
    }
    for (auto& value : hidden) {
        state = state * 1664525U + 1013904223U;
        value = static_cast<u8>(state >> 30U);
    }
}

void compare_row(const ViFilter& filter, s32 x, s32 y, bool repeat_lower, std::size_t width) {
    std::vector<ViColor> actual(width);
    filter.sample_row(x, y, repeat_lower, actual);
    for (std::size_t index = 0; index < width; ++index)
        CHECK_EQ(actual[index], filter.sample(x + static_cast<s32>(index), y, repeat_lower));
}

} // namespace

TEST(vi_filter_cached_rows_match_scalar_across_controls_and_tile_boundaries) {
    constexpr std::array<std::size_t, 6> widths{31, 32, 127, 128, 129, 257};
    for (const std::size_t size : {512U, 515U}) {
        std::vector<u8> bytes(size);
        std::vector<u8> hidden((size + 1) / 2);
        fill_source(bytes, hidden);
        for (unsigned format : {2U, 3U}) {
            for (unsigned aa_mode = 0; aa_mode < 4; ++aa_mode) {
                for (const bool divot : {false, true}) {
                    for (const bool dedither : {false, true}) {
                        std::array<u32, 14> registers{};
                        registers[0] =
                            format | (aa_mode << 8U) | (divot ? 16U : 0U) | (dedither ? 0x10000U : 0U);
                        registers[1] = static_cast<u32>(size - 2U);
                        registers[2] = 137;
                        const ViFilter filter(bytes, hidden, registers);
                        for (const bool repeat_lower : {false, true})
                            for (const std::size_t width : widths)
                                compare_row(filter, -37, -3, repeat_lower, width);
                    }
                }
            }
        }
    }
}

TEST(vi_filter_cached_rows_preserve_partial_hidden_span_and_wrapped_rgba16_reads) {
    std::vector<u8> bytes(263);
    std::vector<u8> hidden(19);
    fill_source(bytes, hidden);
    for (unsigned aa_mode = 0; aa_mode < 4; ++aa_mode) {
        std::array<u32, 14> registers{};
        registers[0] = 2U | (aa_mode << 8U) | 16U | 0x10000U;
        registers[1] = 261;
        registers[2] = 37;
        const ViFilter filter(bytes, hidden, registers);
        compare_row(filter, -19, -5, false, 133);
        compare_row(filter, -19, -5, true, 133);
    }
}

TEST(vi_filter_cached_rows_preserve_repeated_lower_row_at_tile_edges) {
    std::vector<u8> bytes(1021);
    std::vector<u8> hidden((bytes.size() + 1) / 2);
    fill_source(bytes, hidden);
    for (unsigned format : {2U, 3U}) {
        std::array<u32, 14> registers{};
        registers[0] = format | 16U | 0x10000U;
        registers[1] = 1019;
        registers[2] = 73;
        const ViFilter filter(bytes, hidden, registers);
        compare_row(filter, -65, 4, true, 129);
        compare_row(filter, -65, 4, true, 258);
    }
}

TEST(vi_filter_cached_rows_do_not_extend_reads_past_coordinate_limits) {
    std::vector<u8> bytes(512);
    std::vector<u8> hidden(256);
    fill_source(bytes, hidden);
    std::array<u32, 14> registers{};
    registers[0] = 2U | (3U << 8U) | 0x10000U;
    registers[2] = 1;
    const ViFilter filter(bytes, hidden, registers);
    compare_row(filter, std::numeric_limits<s32>::min() + 2, 0, false, 32);
    compare_row(filter, std::numeric_limits<s32>::max() - 33, 0, false, 32);
    compare_row(filter, -37, std::numeric_limits<s32>::max(), true, 129);
}
