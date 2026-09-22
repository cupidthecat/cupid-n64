#include "cupid/vi/filter.hpp"
#include "test.hpp"

#include <array>
#include <vector>

using namespace cupid;

TEST(vi_filtered_rows_match_scalar_neighborhoods_for_all_filter_controls) {
    for (const std::size_t size : {1U, 2U, 3U, 5U, 511U, 512U, 1024U}) {
        std::vector<u8> bytes(size);
        std::vector<u8> hidden((size + 1) / 2);
        u32 noise = 0x1937a57b;
        for (auto& byte : bytes) {
            noise = noise * 1664525U + 1013904223U;
            byte = static_cast<u8>(noise >> 24);
        }
        for (auto& byte : hidden) {
            noise = noise * 1664525U + 1013904223U;
            byte = static_cast<u8>(noise >> 30);
        }
        for (unsigned format : {2U, 3U}) {
            for (unsigned mode = 0; mode < 4; ++mode) {
                for (unsigned flags : {0U, 16U, 0x10000U, 0x10010U}) {
                    std::array<u32, 14> registers{};
                    registers[0] = format | (mode << 8) | flags;
                    registers[1] = static_cast<u32>(size - 1);
                    registers[2] = 13;
                    const ViFilter filter(bytes, hidden, registers);
                    for (const bool repeat_lower : {false, true}) {
                        for (const s32 y : {-7, -1, 0, 1, 5}) {
                            std::array<ViColor, 39> row;
                            filter.sample_row(-9, y, repeat_lower, row);
                            for (unsigned index = 0; index < row.size(); ++index)
                                CHECK_EQ(row[index],
                                         filter.sample(static_cast<s32>(index) - 9, y, repeat_lower));
                        }
                    }
                }
            }
        }
    }
}

TEST(vi_filter_fetch_wraps_unaligned_words_in_odd_sized_memory) {
    constexpr std::array<u8, 5> bytes{0x12, 0x34, 0x56, 0x78, 0x9a};
    std::array<u32, 14> registers{};
    registers[0] = 0x303;
    registers[1] = 4;
    registers[2] = 3;
    const ViFilter rgba32(bytes, {}, registers);
    CHECK_EQ(rgba32.sample(0, 0), (ViColor{0x9a, 0x12, 0x34}));
    CHECK_EQ(rgba32.sample(-2, 0), (ViColor{0x34, 0x56, 0x78}));
    CHECK_EQ(rgba32.sample(0, -1), (ViColor{0x56, 0x78, 0x9a}));

    registers[0] = 0x302;
    const ViFilter rgba16(bytes, {}, registers);
    // The final byte wraps to the first byte: 0x9a12 in RGBA5551.
    CHECK_EQ(rgba16.sample(0, 0), (ViColor{152, 64, 72}));
}

TEST(vi_filter_rows_accept_empty_output_and_empty_memory) {
    std::array<u32, 14> registers{};
    registers[0] = 0x10013;
    const ViFilter filter({}, {}, registers);
    filter.sample_row(0, 0, false, {});
    std::array<ViColor, 3> row{ViColor{1, 2, 3}, ViColor{4, 5, 6}, ViColor{7, 8, 9}};
    filter.sample_row(-1, 0, true, row);
    for (const auto& color : row)
        CHECK_EQ(color, (ViColor{}));
}
