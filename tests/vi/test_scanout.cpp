#include "cupid/system.hpp"
#include "test.hpp"

#include <algorithm>

namespace {
using namespace cupid;

void reg(Bus& bus, unsigned index, u32 value) {
    bus.write(0x04400000U + index * 4, 4, value);
}

void configure(System& system, unsigned format = 3) {
    auto& bus = system.bus;
    const bool pal = system.video_standard() == VideoStandard::Pal;
    reg(bus, 0, 0x300U | format);
    reg(bus, 1, 0x8000);
    reg(bus, 2, 32);
    reg(bus, 9, ((pal ? 128U : 108U) << 16) | (pal ? 168U : 148U));
    reg(bus, 10, ((pal ? 44U : 34U) << 16) | (pal ? 52U : 42U));
    reg(bus, 12, 1024);
    reg(bus, 13, 1024);
}

void pixel(Bus& bus, u32 x, u32 y, u32 color, unsigned bytes = 4, u32 origin = 0x8000, u32 stride = 32) {
    for (unsigned byte = 0; byte < bytes; ++byte) {
        const auto address = (origin + (y * stride + x) * bytes + byte) % bus.rdram.size();
        bus.rdram[address] = static_cast<u8>(color >> ((bytes - 1 - byte) * 8));
    }
}

u32 at(const VideoField& field, unsigned x, unsigned y = 0) {
    return field.pixels[y * field.width + x];
}
} // namespace

TEST(vi_snapshot_has_fixed_field_dimensions_and_opaque_black_borders) {
    for (auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        System system(standard);
        const auto field = system.bus.scan_video();
        CHECK_EQ(field.width, 640U);
        CHECK_EQ(field.height, standard == VideoStandard::Pal ? 288U : 240U);
        CHECK_EQ(field.pixels.size(), field.width * field.height);
        CHECK(std::all_of(field.pixels.begin(), field.pixels.end(), [](u32 p) { return p == 255U; }));
    }
}

TEST(vi_snapshot_decodes_rgba32_and_ignores_stored_alpha) {
    System system;
    configure(system);
    pixel(system.bus, 8, 0, 0x81432500);
    pixel(system.bus, 9, 0, 0x18395aff);
    const auto field = system.bus.scan_video();
    CHECK_EQ(at(field, 8), 0x814325ffU);
    CHECK_EQ(at(field, 9), 0x18395affU);
    CHECK_EQ(at(field, 7), 255U);
    CHECK_EQ(at(field, 33), 255U);
}

TEST(vi_snapshot_expands_rgba16_without_replicating_low_color_bits) {
    System system;
    configure(system, 2);
    for (u32 value = 0; value < 32; ++value) {
        pixel(system.bus, 8, 0, (value << 11) | ((31 - value) << 6) | (value << 1) | (value & 1), 2);
        const u32 expected = (value << 27) | ((31 - value) << 19) | (value << 11) | 255U;
        CHECK_EQ(at(system.bus.scan_video(), 8), expected);
    }
}

TEST(vi_snapshot_uses_pal_display_offsets) {
    System system(VideoStandard::Pal);
    configure(system);
    pixel(system.bus, 8, 0, 0xaabbcc00);
    CHECK_EQ(at(system.bus.scan_video(), 8), 0xaabbccffU);
    reg(system.bus, 9, (138U << 16) | 178U);
    reg(system.bus, 10, (48U << 16) | 56U);
    const auto field = system.bus.scan_video();
    CHECK_EQ(at(field, 18, 2), 0xaabbccffU);
    CHECK_EQ(at(field, 18, 1), 255U);
}

TEST(vi_snapshot_scales_coordinates_and_offsets_in_ten_fractional_bits) {
    System system;
    configure(system);
    reg(system.bus, 12, (1536U << 16) | 512U);
    reg(system.bus, 13, (2048U << 16) | 2048U);
    pixel(system.bus, 5, 2, 0x12345600);
    pixel(system.bus, 6, 2, 0xabcdef00);
    pixel(system.bus, 5, 4, 0x45678900);
    const auto field = system.bus.scan_video();
    CHECK_EQ(at(field, 8), 0x123456ffU);
    CHECK_EQ(at(field, 9), 0xabcdefffU);
    CHECK_EQ(at(field, 8, 1), 0x456789ffU);
}

TEST(vi_snapshot_resamples_vertical_before_horizontal_with_five_bit_fractions) {
    System system;
    configure(system);
    reg(system.bus, 0, 0x203);
    reg(system.bus, 12, (512U << 16) | 1024U);
    reg(system.bus, 13, (512U << 16) | 1024U);
    pixel(system.bus, 8, 0, 0x0000ff00);
    pixel(system.bus, 9, 0, 0x01000100);
    pixel(system.bus, 8, 1, 0x01000100);
    pixel(system.bus, 9, 1, 0x0200ff00);
    CHECK_EQ(at(system.bus.scan_video(), 8), 0x020080ffU);
    reg(system.bus, 12, (543U << 16) | 1024U);
    reg(system.bus, 13, (543U << 16) | 1024U);
    CHECK_EQ(at(system.bus.scan_video(), 8), 0x020080ffU);
}

TEST(vi_snapshot_replication_ignores_fractional_coordinates) {
    System system;
    configure(system);
    reg(system.bus, 12, (1023U << 16) | 1024U);
    reg(system.bus, 13, (1023U << 16) | 1024U);
    pixel(system.bus, 8, 0, 0xaabbcc00);
    pixel(system.bus, 9, 1, 0xffffff00);
    CHECK_EQ(at(system.bus.scan_video(), 8), 0xaabbccffU);
}

TEST(vi_snapshot_zero_scales_repeat_the_selected_source_pixel) {
    System system;
    configure(system);
    reg(system.bus, 12, 2048U << 16);
    reg(system.bus, 13, 1024U << 16);
    pixel(system.bus, 2, 1, 0x76543200);
    const auto field = system.bus.scan_video();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 8; x < 33; ++x)
            CHECK_EQ(at(field, x, y), 0x765432ffU);
}

TEST(vi_snapshot_clipped_windows_preserve_source_coordinates) {
    System system;
    configure(system);
    reg(system.bus, 9, (104U << 16) | 752U);
    reg(system.bus, 10, (30U << 16) | 40U);
    pixel(system.bus, 4, 2, 0xabcdef00);
    pixel(system.bus, 643, 2, 0x12345600);
    const auto field = system.bus.scan_video();
    CHECK_EQ(at(field, 0), 0xabcdefffU);
    CHECK_EQ(at(field, 639), 0x123456ffU);
    CHECK_EQ(at(field, 0, 3), 255U);
}

TEST(vi_snapshot_guard_bands_depend_on_unclipped_horizontal_endpoints) {
    System system;
    configure(system);
    std::fill(system.bus.rdram.begin(), system.bus.rdram.end(), u8{255});
    reg(system.bus, 9, (108U << 16) | 748U);
    auto field = system.bus.scan_video();
    CHECK_EQ(at(field, 7), 255U);
    CHECK_EQ(at(field, 8), 0xffffffffU);
    CHECK_EQ(at(field, 632), 0xffffffffU);
    CHECK_EQ(at(field, 633), 255U);
    reg(system.bus, 9, (107U << 16) | 749U);
    field = system.bus.scan_video();
    CHECK_EQ(at(field, 0), 0xffffffffU);
    CHECK_EQ(at(field, 639), 0xffffffffU);
}

TEST(vi_snapshot_blank_reserved_and_reversed_windows_are_black) {
    System system;
    configure(system);
    std::fill(system.bus.rdram.begin(), system.bus.rdram.end(), u8{255});
    const auto black = [&] {
        const auto field = system.bus.scan_video();
        CHECK(std::all_of(field.pixels.begin(), field.pixels.end(), [](u32 p) { return p == 255U; }));
    };
    for (u32 format : {0U, 1U}) {
        reg(system.bus, 0, 0x300U | format);
        black();
    }
    configure(system);
    reg(system.bus, 9, (148U << 16) | 108U);
    black();
    configure(system);
    reg(system.bus, 10, (42U << 16) | 34U);
    black();
    configure(system);
    reg(system.bus, 9, (108U << 16) | 122U);
    black();
}

TEST(vi_snapshot_aligns_origin_and_wraps_installed_rdram) {
    for (unsigned mebibytes : {4U, 8U}) {
        for (unsigned format : {2U, 3U}) {
            System system;
            system.bus.rdram.resize(mebibytes * 1024 * 1024);
            configure(system, format);
            const unsigned bytes = format == 2 ? 2 : 4;
            reg(system.bus, 1, static_cast<u32>(system.bus.rdram.size()) - 1);
            reg(system.bus, 12, 0);
            pixel(system.bus, 0, 0, format == 2 ? 0xf801 : 0xff0000e0, bytes,
                  static_cast<u32>(system.bus.rdram.size()) - bytes);
            pixel(system.bus, 0, 0, format == 2 ? 0x07c1 : 0x00ff00e0, bytes, 0);
            CHECK_EQ(at(system.bus.scan_video(), 8), format == 2 ? 0xf80000ffU : 0xff0000ffU);
            reg(system.bus, 12, 1024U << 16);
            CHECK_EQ(at(system.bus.scan_video(), 8), format == 2 ? 0x00f800ffU : 0x00ff00ffU);
        }
    }
}

TEST(vi_snapshot_uses_width_as_stride_without_clamping_source_x) {
    System system;
    configure(system);
    reg(system.bus, 2, 3);
    pixel(system.bus, 8, 0, 0xaabbcc00, 4, 0x8000, 3);
    pixel(system.bus, 8, 1, 0x12345600, 4, 0x8000, 3);
    const auto field = system.bus.scan_video();
    CHECK_EQ(at(field, 8), 0xaabbccffU);
    CHECK_EQ(at(field, 8, 1), 0x123456ffU);
}

TEST(vi_snapshot_tracks_field_parity_without_advancing_time) {
    System system;
    configure(system);
    reg(system.bus, 0, 0x343);
    reg(system.bus, 6, 4);
    reg(system.bus, 7, 99);
    reg(system.bus, 8, (100U << 16) | 100U);
    const auto first = system.bus.scan_video();
    CHECK(first.interlaced);
    CHECK_EQ(first.field, 0U);
    system.bus.tick((300ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency());
    const auto second = system.bus.scan_video();
    CHECK_EQ(second.field, 1U);
    CHECK_EQ(system.bus.read(0x04400010, 4), 1U);
    CHECK_EQ(system.bus.scan_video().pixels, second.pixels);
    CHECK_EQ(system.bus.read(0x04400010, 4), 1U);
    reg(system.bus, 0, 0x303);
    CHECK(!system.bus.scan_video().interlaced);
}

TEST(vi_snapshot_observes_register_and_memory_changes_without_mutating_prior_output) {
    System system;
    configure(system);
    pixel(system.bus, 8, 0, 0x12345600);
    const auto first = system.bus.scan_video();
    pixel(system.bus, 8, 0, 0xabcdef00);
    CHECK_EQ(at(system.bus.scan_video(), 8), 0xabcdefffU);
    CHECK_EQ(at(first, 8), 0x123456ffU);
    reg(system.bus, 1, 0x8100);
    CHECK_EQ(at(system.bus.scan_video(), 8), 255U);
}

TEST(vi_snapshot_does_not_touch_bus_latches_or_rdram_bank_state) {
    System system;
    configure(system);
    pixel(system.bus, 8, 0, 0x12345600);
    const auto banks = system.bus.memory.bank_status();
    const auto errors = system.bus.memory.errors();
    const auto current = system.bus.read(0x04400010, 4);
    CHECK_EQ(at(system.bus.scan_video(), 8), 0x123456ffU);
    CHECK_EQ(system.bus.memory.bank_status(), banks);
    CHECK_EQ(system.bus.memory.errors(), errors);
    CHECK_EQ(system.bus.read(0x04400010, 4), current);
}
