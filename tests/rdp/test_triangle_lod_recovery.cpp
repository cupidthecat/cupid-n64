#include "triangle_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {

void check_lod_recovery(bool left_major) {
    TriangleCommands commands;
    commands.system->bus.rdp.set_parallel_rasterization(false);
    commands.tile(3, 96, (1ULL << 19U) | (1ULL << 9U));
    commands.append(0x32, 3ULL << 24U);
    commands.texture.value = {0, 0, left_major ? 0U : 0xf0000000U, 0};
    commands.texture.dx = {0, 0, left_major ? 0x10000000U : 0xf0000000U, 0};
    commands.texture.de = {};
    commands.texture.dy = {};
    commands.append(0x3c, combine_word({}, {.d = 1, .ad = 1}));
    commands.modes((1ULL << 51U) | (1ULL << 48U));
    commands.triangle(10, {.middle = 4,
                           .bottom = 4,
                           .major = left_major ? 0 : 0x40000,
                           .upper = left_major ? 0x40000 : 0,
                           .lower = left_major ? 0x40000 : 0,
                           .left_major = left_major,
                           .maximum_level = 3});
    commands.run();

    // W is zero at the first pixel, then 4096, 8192, and 12288. With S=T=0, each
    // later pixel and both LOD neighbors have valid, identical coordinates.
    // Their magnification must select tile zero after the initial overflow.
    CHECK_EQ(commands.pixel(left_major ? 0U : 3U), TextureCommands::expected(2, 3));
    for (unsigned step = 1; step < 4; ++step)
        CHECK_EQ(commands.pixel(left_major ? step : 3U - step), TextureCommands::expected(0, 0));
    CHECK_EQ(commands.pixel(4), 0U);
}

} // namespace

TEST(rdp_triangle_lod_recovers_after_nonpositive_perspective_w) {
    check_lod_recovery(true);
}

TEST(rdp_triangle_lod_recovers_in_reverse_span_direction) {
    check_lod_recovery(false);
}
