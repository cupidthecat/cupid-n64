#include "cupid/system.hpp"
#include "filter_fixture.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>

namespace {
using namespace cupid;
using test::vi::FilterFixture;

struct SnapshotFixture {
    System system;
    FilterFixture image;

    explicit SnapshotFixture(unsigned format = 3, VideoStandard standard = VideoStandard::Ntsc)
        : system(standard), image(format) {
        test::initialize_memory(system);
        const unsigned horizontal = standard == VideoStandard::Pal ? 128U : 108U;
        const unsigned vertical = standard == VideoStandard::Pal ? 44U : 34U;
        image.registers[9] = (horizontal << 16) | (horizontal + 16U);
        image.registers[10] = (vertical << 16) | (vertical + 2U);
        image.registers[12] = 1024;
        image.registers[13] = (3072U << 16) | 1024U;
    }
    void write(unsigned index, u32 value) {
        system.bus.write(0x04400000U + index * 4, 4, value);
    }
    VideoField scan() {
        std::copy(image.bytes.begin(), image.bytes.end(), system.bus.rdram.begin() + 0x8000);
        for (unsigned pair = 0; pair < image.hidden.size(); ++pair)
            system.bus.memory.set_hidden_pair(0x8000U + pair * 2, image.hidden[pair]);
        for (unsigned index : {0U, 2U, 9U, 10U, 12U, 13U})
            write(index, image.registers[index]);
        write(1, 0x8000);
        return system.bus.scan_video();
    }
    u32 pixel() {
        return scan().pixels[8];
    }
};
} // namespace

TEST(vi_scanout_filters_rgba32_coverage_in_both_antialias_modes) {
    for (u32 mode : {0U, 1U}) {
        SnapshotFixture fixture;
        fixture.image.registers[0] |= mode << 8;
        fixture.image.gray(8, 3, 80, 0);
        fixture.image.neighbors({10, 20, 120, 180, 200, 220});
        CHECK_EQ(fixture.pixel(), 0x858585ffU);
    }
}

TEST(vi_scanout_reads_rdram_hidden_pairs_for_rgba16_coverage) {
    SnapshotFixture fixture(2);
    fixture.image.neighbors({8, 16, 120, 176, 200, 216});
    for (u32 coverage = 0; coverage < 8; ++coverage) {
        fixture.image.gray(8, 3, 80, coverage);
        const u32 value = 80 + ((56 * (7 - coverage) + 4) >> 3);
        CHECK_EQ(fixture.pixel(), (value << 24) | (value << 16) | (value << 8) | 255U);
    }
}

TEST(vi_scanout_filters_before_bilinear_resampling) {
    SnapshotFixture fixture;
    fixture.image.gray(8, 3, 80, 0);
    fixture.image.neighbors({10, 20, 120, 180, 200, 220});
    fixture.image.gray(9, 3, 200);
    fixture.image.registers[12] |= 512U << 16;
    CHECK_EQ(fixture.pixel(), 0xa7a7a7ffU);
}

TEST(vi_scanout_applies_divot_to_filtered_colors) {
    SnapshotFixture fixture;
    fixture.image.registers[0] |= 16;
    fixture.image.put(7, 3, {10, 200, 30}, 0);
    fixture.image.put(8, 3, {100, 20, 250});
    fixture.image.put(9, 3, {80, 160, 40}, 0);
    CHECK_EQ(fixture.pixel(), 0x50a028ffU);
    fixture.image.registers[0] &= ~16U;
    CHECK_EQ(fixture.pixel(), 0x6414faffU);
}

TEST(vi_scanout_dither_restoration_control_is_retained_without_changing_readback) {
    SnapshotFixture fixture;
    fixture.image.registers[0] |= 0x10000;
    fixture.image.gray(8, 3, 103);
    CHECK_EQ(fixture.pixel(), 0x585858ffU);
    CHECK_EQ(fixture.system.bus.read(0x04400000, 4), 3U);
    fixture.image.registers[0] &= ~0x10000U;
    CHECK_EQ(fixture.pixel(), 0x676767ffU);
}

TEST(vi_scanout_repeated_rows_use_the_alternate_lower_filter_samples) {
    SnapshotFixture fixture;
    fixture.image.registers[0] = 0x10203;
    fixture.image.registers[10] = (34U << 16) | 40U;
    fixture.image.registers[13] = (3072U << 16) | 512U;
    for (s32 y = 2; y <= 5; ++y)
        for (s32 x = 0; x < 16; ++x)
            fixture.image.gray(x, y, y == 5 ? 128 : 64);
    const auto field = fixture.scan();
    CHECK_EQ(field.pixels[8], 0x404040ffU);
    CHECK_EQ(field.pixels[640 + 8], 0x404040ffU);
    CHECK_EQ(field.pixels[1280 + 8], 0x434343ffU);
    fixture.image.registers[13] = (3584U << 16) | 512U;
    CHECK_EQ(fixture.pixel(), 0x424242ffU);
}

TEST(vi_scanout_clipped_first_line_uses_normal_dither_neighbors) {
    for (const auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        const unsigned vertical = standard == VideoStandard::Pal ? 44U : 34U;
        for (const unsigned format : {2U, 3U}) {
            for (const unsigned clipped : {0U, 1U, 2U, 3U}) {
                SnapshotFixture fixture(format, standard);
                fixture.image.registers[0] = 0x10200U | format;
                fixture.image.registers[10] = (vertical << 16) | (vertical + (clipped + 2U) * 2U);
                fixture.image.registers[13] = ((3584U - clipped * 512U) << 16) | 512U;
                for (s32 y = 2; y <= 5; ++y)
                    for (s32 x = 0; x < 16; ++x)
                        fixture.image.gray(x, y, y == 5 ? 128 : 64);
                const auto full = fixture.scan();
                const u32 expected = clipped == 0 ? 0x424242ffU : 0x404040ffU;
                CHECK_EQ(full.pixels[clipped * full.width + 8U], expected);
                fixture.image.registers[10] = ((vertical - clipped * 2U) << 16) | (vertical + 4U);
                CHECK_EQ(fixture.pixel(), 0x424242ffU);
            }
        }
    }
}

TEST(vi_scanout_clipped_first_line_uses_normal_coverage_neighbors) {
    for (const auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        const unsigned vertical = standard == VideoStandard::Pal ? 44U : 34U;
        for (const unsigned format : {2U, 3U}) {
            SnapshotFixture fixture(format, standard);
            fixture.image.registers[10] = (vertical << 16) | (vertical + 6U);
            fixture.image.registers[13] = (3072U << 16) | 512U;
            fixture.image.gray(8, 3, 80);
            fixture.image.gray(8, 4, 80, 0);
            fixture.image.gray(7, 3, 8);
            fixture.image.gray(9, 3, 16);
            fixture.image.gray(6, 4, 120);
            fixture.image.gray(10, 4, 176);
            fixture.image.gray(7, 5, 200);
            fixture.image.gray(9, 5, 216);
            const auto full = fixture.scan();
            CHECK_EQ(full.pixels[full.width + 8U], 0x5e5e5effU);
            fixture.image.registers[10] = ((vertical - 2U) << 16) | (vertical + 4U);
            CHECK_EQ(fixture.pixel(), 0x696969ffU);
        }
    }
}

TEST(vi_scanout_gamma_runs_after_resampling_in_both_framebuffer_sizes) {
    for (unsigned format : {2U, 3U}) {
        SnapshotFixture fixture(format);
        fixture.image.registers[0] |= 0x208;
        fixture.image.registers[12] |= 512U << 16;
        fixture.image.gray(8, 3, 80);
        fixture.image.gray(9, 3, 160);
        CHECK_EQ(fixture.pixel(), 0xaeaeaeffU);
    }
}

TEST(vi_scanout_gamma_dither_is_repeatable_and_advances_only_with_fields) {
    SnapshotFixture fixture;
    fixture.image.registers[0] |= 0x30c;
    fixture.image.gray(8, 3, 1);
    fixture.image.registers[9] = (108U << 16) | 148U;
    fixture.write(6, 4);
    fixture.write(7, 99);
    fixture.write(8, (100U << 16) | 100U);
    const auto first = fixture.scan();
    CHECK_EQ(first.pixels, fixture.scan().pixels);
    fixture.system.bus.tick((300ULL * 62500000 + fixture.system.video_frequency() - 1) /
                            fixture.system.video_frequency());
    const auto next = fixture.scan();
    CHECK(first.pixels != next.pixels);
    CHECK_EQ(next.field, 1U);
    fixture.system.bus.reset();
    test::initialize_memory(fixture.system);
    CHECK_EQ(fixture.scan().pixels, first.pixels);
}

TEST(vi_scanout_gamma_dither_without_gamma_preserves_saturated_channels) {
    SnapshotFixture fixture;
    fixture.image.registers[0] |= 0x304;
    fixture.image.put(8, 3, {255, 254, 10});
    const auto value = fixture.pixel();
    CHECK_EQ(value >> 24, 255U);
    CHECK(((value >> 16) & 255U) >= 254);
    CHECK(((value >> 8) & 255U) >= 10 && ((value >> 8) & 255U) <= 11);
    CHECK_EQ(value & 255U, 255U);
}

TEST(vi_scanout_filters_leave_blank_pixels_and_memory_state_unchanged) {
    SnapshotFixture fixture(2);
    fixture.image.registers[0] |= 0x1001c;
    fixture.image.gray(8, 3, 80, 0);
    fixture.image.neighbors({8, 16, 120, 176, 200, 216});
    const auto field = fixture.scan();
    const auto banks = fixture.system.bus.memory.bank_status();
    const auto errors = fixture.system.bus.memory.errors();
    const auto bytes = fixture.system.bus.rdram;
    const auto hidden_view = fixture.system.bus.memory.hidden_memory();
    const std::vector<u8> hidden(hidden_view.begin(), hidden_view.end());
    CHECK_EQ(field.pixels[7], 255U);
    CHECK_EQ(field.pixels[9], 255U);
    CHECK_EQ(fixture.system.bus.scan_video().pixels, field.pixels);
    CHECK_EQ(fixture.system.bus.rdram, bytes);
    CHECK(std::equal(hidden.begin(), hidden.end(), fixture.system.bus.memory.hidden_memory().begin()));
    CHECK_EQ(fixture.system.bus.memory.bank_status(), banks);
    CHECK_EQ(fixture.system.bus.memory.errors(), errors);
}
