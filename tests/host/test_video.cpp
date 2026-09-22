#include "cupid/host/video.hpp"
#include "test.hpp"

#include <limits>

using namespace cupid;
using namespace cupid::host;

namespace {
VideoField field(unsigned width, unsigned height, unsigned parity, bool interlaced, std::vector<u32> pixels) {
    return {width, height, parity, interlaced, std::move(pixels)};
}

u32 at(const DisplayFrame& frame, unsigned x, unsigned y) {
    return frame.pixels[static_cast<std::size_t>(y) * frame.width + x];
}
} // namespace

TEST(host_video_progressive_frame_is_exact_and_drops_interlace_history) {
    FrameComposer composer;
    CHECK(composer.compose(field(2, 1, 0, true, {0x11223344, 0x55667788})).has_value());
    const auto frame =
        composer.compose(field(2, 2, 1, false, {0x01020304, 0x11121314, 0xa1a2a3a4, 0xb1b2b3b4}));
    CHECK(frame.has_value());
    CHECK_EQ(frame->width, 2U);
    CHECK_EQ(frame->height, 2U);
    CHECK_EQ(frame->pixels, (std::vector<u32>{0x01020304, 0x11121314, 0xa1a2a3a4, 0xb1b2b3b4}));

    const auto next = composer.compose(field(2, 1, 1, true, {0xc0c1c2c3, 0xd0d1d2d3}));
    CHECK(next.has_value());
    CHECK_EQ(next->height, 2U);
    CHECK_EQ(next->pixels, (std::vector<u32>{0xc0c1c2c3, 0xd0d1d2d3, 0xc0c1c2c3, 0xd0d1d2d3}));
}

TEST(host_video_first_interlaced_field_bobs_literal_rows) {
    FrameComposer composer;
    const auto frame =
        composer.compose(field(2, 2, 1, true, {0x010203ff, 0x112233ff, 0xa0b0c0ff, 0xd0e0f0ff}));
    CHECK(frame.has_value());
    CHECK_EQ(frame->width, 2U);
    CHECK_EQ(frame->height, 4U);
    CHECK_EQ(frame->pixels, (std::vector<u32>{0x010203ff, 0x112233ff, 0x010203ff, 0x112233ff, 0xa0b0c0ff,
                                              0xd0e0f0ff, 0xa0b0c0ff, 0xd0e0f0ff}));
}

TEST(host_video_neighboring_parities_weave_in_output_row_order) {
    FrameComposer composer;
    CHECK(
        composer.compose(field(2, 2, 1, true, {0x110000ff, 0x120000ff, 0x130000ff, 0x140000ff})).has_value());
    const auto frame =
        composer.compose(field(2, 2, 0, true, {0x010000ff, 0x020000ff, 0x030000ff, 0x040000ff}));
    CHECK(frame.has_value());
    CHECK_EQ(frame->pixels, (std::vector<u32>{0x010000ff, 0x020000ff, 0x110000ff, 0x120000ff, 0x030000ff,
                                              0x040000ff, 0x130000ff, 0x140000ff}));
}

TEST(host_video_repeated_parity_bobs_latest_field_then_weaves_it) {
    FrameComposer composer;
    CHECK(composer.compose(field(1, 2, 0, true, {0x100000ff, 0x200000ff})).has_value());
    const auto repeated = composer.compose(field(1, 2, 0, true, {0x300000ff, 0x400000ff}));
    CHECK(repeated.has_value());
    CHECK_EQ(repeated->pixels, (std::vector<u32>{0x300000ff, 0x300000ff, 0x400000ff, 0x400000ff}));

    const auto woven = composer.compose(field(1, 2, 1, true, {0x500000ff, 0x600000ff}));
    CHECK(woven.has_value());
    CHECK_EQ(woven->pixels, (std::vector<u32>{0x300000ff, 0x500000ff, 0x400000ff, 0x600000ff}));
}

TEST(host_video_geometry_change_discards_opposite_parity_history) {
    FrameComposer composer;
    CHECK(composer.compose(field(2, 1, 0, true, {1, 2})).has_value());
    const auto changed = composer.compose(field(1, 2, 1, true, {3, 4}));
    CHECK(changed.has_value());
    CHECK_EQ(changed->pixels, (std::vector<u32>{3, 3, 4, 4}));
}

TEST(host_video_blank_clears_stale_history_and_reset_does_the_same) {
    FrameComposer composer;
    CHECK(composer.compose(field(1, 1, 0, true, {0x123456ff})).has_value());
    const auto blank = composer.compose(VideoField{});
    CHECK(blank.has_value());
    CHECK_EQ(blank->width, 0U);
    CHECK_EQ(blank->height, 0U);
    CHECK(blank->pixels.empty());
    auto next = composer.compose(field(1, 1, 1, true, {0xabcdef01}));
    CHECK(next.has_value());
    CHECK_EQ(next->pixels, (std::vector<u32>{0xabcdef01, 0xabcdef01}));

    composer.reset();
    next = composer.compose(field(1, 1, 0, true, {0x89abcdef}));
    CHECK(next.has_value());
    CHECK_EQ(next->pixels, (std::vector<u32>{0x89abcdef, 0x89abcdef}));

    const auto zero_geometry_blank = composer.compose(VideoField{0, 0, 0, false, {}});
    CHECK(zero_geometry_blank.has_value());
    CHECK_EQ(*zero_geometry_blank, (DisplayFrame{}));
}

TEST(host_video_rejects_malformed_fields_without_destroying_history) {
    FrameComposer composer;
    CHECK(composer.compose(field(1, 1, 0, true, {0x01020304})).has_value());
    CHECK(!composer.compose(field(0, 1, 1, true, {})).has_value());
    CHECK(!composer.compose(field(1, 0, 1, true, {})).has_value());
    CHECK(!composer.compose(field(1, 1, 2, true, {0})).has_value());
    CHECK(!composer.compose(field(2, 2, 1, true, {0, 1, 2})).has_value());
    CHECK(!composer.compose(field(max_video_field_width + 1U, 1, 1, true, {})).has_value());
    CHECK(!composer.compose(field(1, max_video_field_height + 1U, 1, true, {})).has_value());
    CHECK(!composer.compose(field(std::numeric_limits<unsigned>::max(), 2, 1, true, {})).has_value());

    const auto frame = composer.compose(field(1, 1, 1, true, {0xa0b0c0d0}));
    CHECK(frame.has_value());
    CHECK_EQ(frame->pixels, (std::vector<u32>{0x01020304, 0xa0b0c0d0}));
}

TEST(host_video_accepts_core_maximum_geometry_without_overflow) {
    FrameComposer composer;
    std::vector<u32> pixels(static_cast<std::size_t>(max_video_field_width) * max_video_field_height,
                            0x102030ff);
    pixels.front() = 0x010203ff;
    pixels.back() = 0xf1f2f3ff;
    const auto frame =
        composer.compose(field(max_video_field_width, max_video_field_height, 0, true, std::move(pixels)));
    CHECK(frame.has_value());
    CHECK_EQ(frame->width, 640U);
    CHECK_EQ(frame->height, 576U);
    CHECK_EQ(at(*frame, 0, 0), 0x010203ffU);
    CHECK_EQ(at(*frame, 0, 1), 0x010203ffU);
    CHECK_EQ(at(*frame, 639, 574), 0xf1f2f3ffU);
    CHECK_EQ(at(*frame, 639, 575), 0xf1f2f3ffU);
}

TEST(host_video_letterbox_4_3_centers_and_handles_small_windows) {
    CHECK_EQ(letterbox_4_3(640, 480), (DisplayRect{0, 0, 640, 480}));
    CHECK_EQ(letterbox_4_3(1280, 720), (DisplayRect{160, 0, 960, 720}));
    CHECK_EQ(letterbox_4_3(600, 900), (DisplayRect{0, 225, 600, 450}));
    CHECK_EQ(letterbox_4_3(3, 2), (DisplayRect{0, 0, 3, 2}));
    CHECK_EQ(letterbox_4_3(1, 1), (DisplayRect{0, 0, 1, 1}));
    CHECK_EQ(letterbox_4_3(0, 10), (DisplayRect{}));
    CHECK_EQ(letterbox_4_3(10, 0), (DisplayRect{}));
}
