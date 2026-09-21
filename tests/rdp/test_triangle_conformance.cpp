#include "test.hpp"
#include "test_system.hpp"

#include "cupid/system.hpp"

#include <array>
#include <memory>
#include <span>

using namespace cupid;

namespace {

struct RawTriangleFixture {
    std::unique_ptr<System> system = std::make_unique<System>();

    RawTriangleFixture() {
        test::initialize_memory(*system);
    }

    void run(std::span<const u64> words) {
        u32 end = 0x1000;
        for (const u64 word : words) {
            system->bus.write(end, 8, word);
            end += 8;
        }
        system->bus.rdp.write_register(0, 0x1000);
        system->bus.rdp.write_register(4, end);
        CHECK_EQ(system->bus.rdp.current(), end);
        CHECK_EQ(system->bus.rdp.read_register(12) & 0x62U, 0U);
    }

    [[nodiscard]] u32 pixel(unsigned x = 0, unsigned y = 0) const {
        return static_cast<u32>(system->bus.memory.read(0x8000U + (y * 8U + x) * 4U, 4));
    }

    [[nodiscard]] u16 depth(unsigned x, unsigned y = 0) const {
        return static_cast<u16>(system->bus.memory.read(0x9000U + (y * 8U + x) * 2U, 2));
    }
};

constexpr u64 set_framebuffer = 0x3f18000700008000ULL;
constexpr u64 set_combiner = 0x3c887f1088fdf6fbULL;
constexpr u64 set_primitive = 0x3a000000804020ffULL;
constexpr u64 set_primitive_white = 0x3a000000ffffffffULL;
constexpr u64 set_scissor_8x8 = 0x2d00000000020020ULL;
constexpr u64 sync_full = 0x2900000000000000ULL;

} // namespace

TEST(rdp_raw_triangle_corrected_rgba_blend_payload_matches_literal_pixels) {
    constexpr std::array<u64, 11> packet{
        set_framebuffer,
        set_combiner,
        set_primitive_white,
        0x39000000ff0000ffULL,
        0x2f0000f080000000ULL,
        set_scissor_8x8,
        0x08800014000c0004ULL,
        0x0003000000000000ULL,
        0x0001000000000000ULL,
        0x0005000000000000ULL,
        sync_full,
    };
    constexpr std::array expected_rows{
        std::array<u32, 8>{0, 0, 0, 0, 0, 0, 0, 0},
        std::array<u32, 8>{0, 0xff0000e0U, 0xff0000e0U, 0xff0000e0U, 0xff0000e0U, 0, 0, 0},
        std::array<u32, 8>{0, 0xff0000e0U, 0xff0000e0U, 0xff0000e0U, 0xff0000e0U, 0, 0, 0},
        std::array<u32, 8>{0, 0xff0000e0U, 0xff0000e0U, 0, 0, 0, 0, 0},
        std::array<u32, 8>{0, 0xff0000e0U, 0xff0000e0U, 0, 0, 0, 0, 0},
        std::array<u32, 8>{0, 0, 0, 0, 0, 0, 0, 0},
        std::array<u32, 8>{0, 0, 0, 0, 0, 0, 0, 0},
        std::array<u32, 8>{0, 0, 0, 0, 0, 0, 0, 0},
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    for (unsigned y = 0; y < expected_rows.size(); ++y)
        for (unsigned x = 0; x < expected_rows[y].size(); ++x)
            CHECK_EQ(fixture.pixel(x, y), expected_rows[y][x]);
}

TEST(rdp_raw_triangle_argb_fixture_payload_is_interpreted_as_rgba_bytes) {
    constexpr std::array<u64, 11> packet{
        set_framebuffer,
        set_combiner,
        set_primitive_white,
        0x39000000ffff0000ULL,
        0x2f0000f080000000ULL,
        set_scissor_8x8,
        0x08800014000c0004ULL,
        0x0003000000000000ULL,
        0x0001000000000000ULL,
        0x0005000000000000ULL,
        sync_full,
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    CHECK_EQ(fixture.pixel(1, 1), 0xffff00e0U);
    CHECK_EQ(fixture.pixel(4, 2), 0xffff00e0U);
    CHECK_EQ(fixture.pixel(3, 3), 0U);
}

TEST(rdp_raw_triangle_diagonal_uses_eight_staggered_coverage_samples) {
    constexpr std::array<u64, 10> packet{
        set_framebuffer,       set_combiner,
        set_primitive,         0x2f0000f000000008ULL,
        set_scissor_8x8,       0x0880001000100000ULL,
        0x0004000000000000ULL, 0x0000000000000000ULL,
        0x00040000ffff0000ULL, sync_full,
    };
    constexpr std::array expected{
        std::array<u32, 4>{0x804020e0U, 0x804020e0U, 0x804020e0U, 0x80402060U},
        std::array<u32, 4>{0x804020e0U, 0x804020e0U, 0x80402060U, 0},
        std::array<u32, 4>{0x804020e0U, 0x80402060U, 0, 0},
        std::array<u32, 4>{0x80402060U, 0, 0, 0},
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    for (unsigned y = 0; y < expected.size(); ++y)
        for (unsigned x = 0; x < expected[y].size(); ++x)
            CHECK_EQ(fixture.pixel(x, y), expected[y][x]);
}

TEST(rdp_raw_triangle_fractional_top_without_aa_rejects_missing_first_sample) {
    constexpr std::array<u64, 10> packet{
        set_framebuffer,       set_combiner,
        set_primitive,         0x2f0000f000000000ULL,
        set_scissor_8x8,       0x0880000700070001ULL,
        0x0004000000000000ULL, 0x0000000000000000ULL,
        0x0004000000000000ULL, sync_full,
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    for (unsigned x = 0; x < 4; ++x) {
        CHECK_EQ(fixture.pixel(x, 0), 0U);
        CHECK_EQ(fixture.pixel(x, 1), 0x804020a0U);
    }
}

TEST(rdp_raw_triangle_middle_before_top_uses_lower_edge) {
    constexpr std::array<u64, 11> packet{
        set_framebuffer,
        set_combiner,
        set_primitive_white,
        0x39000000ff0000ffULL,
        0x2f0000f080000000ULL,
        set_scissor_8x8,
        0x088000103ffc0004ULL,
        0x0004000000000000ULL,
        0x0002000000000000ULL,
        0x0000000000000000ULL,
        sync_full,
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    CHECK_EQ(fixture.pixel(1, 1), 0U);
    CHECK_EQ(fixture.pixel(2, 1), 0xff0000e0U);
    CHECK_EQ(fixture.pixel(3, 3), 0xff0000e0U);
}

TEST(rdp_raw_triangle_zero_height_packet_is_rejected_without_writes) {
    constexpr std::array<u64, 10> packet{
        set_framebuffer,       set_combiner,
        set_primitive,         0x2f0000f000000008ULL,
        set_scissor_8x8,       0x0880000400040004ULL,
        0x0004000000000000ULL, 0x0000000000000000ULL,
        0x0004000000000000ULL, sync_full,
    };

    RawTriangleFixture fixture;
    fixture.system->bus.memory.write(0x8000, 4, 0x11223344U);
    fixture.run(packet);
    CHECK_EQ(fixture.pixel(), 0x11223344U);
    CHECK_EQ(fixture.pixel(1), 0U);
}

TEST(rdp_raw_triangle_edge_overflow_wraps_28bit_before_coverage) {
    constexpr std::array<u64, 10> packet{
        set_framebuffer,       set_combiner,
        set_primitive,         0x2f0000f000000008ULL,
        0x2d00000000020004ULL, 0x0880000400040000ULL,
        0x07ff000020040000ULL, 0x0000000000000000ULL,
        0x07ff000020040000ULL, sync_full,
    };
    constexpr std::array<u32, 8> expected{
        0x80402060U, 0x80402060U, 0x80402020U, 0x80402020U,
        0x80402020U, 0x80402020U, 0x80402020U, 0x80402020U,
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    for (unsigned x = 0; x < expected.size(); ++x)
        CHECK_EQ(fixture.pixel(x), expected[x]);
    CHECK_EQ(fixture.pixel(0, 1), 0U);
}

TEST(rdp_raw_triangle_depth_packet_interpolates_literal_z_words) {
    constexpr std::array<u64, 13> packet{
        set_framebuffer,
        set_combiner,
        set_primitive,
        0x3e00000000009000ULL,
        0x2f0000f000000020ULL,
        set_scissor_8x8,
        0x0980000800080000ULL,
        0x0004000000000000ULL,
        0x0000000000000000ULL,
        0x0004000000000000ULL,
        0x7000000000010000ULL,
        0x0000000000000000ULL,
        sync_full,
    };

    RawTriangleFixture fixture;
    fixture.run(packet);
    CHECK_EQ(fixture.pixel(), 0x804020e0U);
    CHECK_EQ(fixture.depth(0), 0x6000U);
    CHECK_EQ(fixture.depth(1), 0x6004U);
    CHECK_EQ(fixture.system->bus.memory.hidden_pair(0x9000), 1U);
}
