#include "triangle_commands.hpp"

using namespace cupid;
using namespace test::rdp;

namespace {

constexpr u32 depth_address = 0x100000U;
constexpr u64 z_compare = 1ULL << 4U;
constexpr u64 alpha_compare = 1U;

bool depth_bank_open_after_alpha(unsigned alpha) {
    TriangleCommands commands;
    auto& memory = commands.system->bus.memory;

    commands.append(0x3e, depth_address);
    commands.append(0x39, 0x000000ffU);
    commands.append(0x3a, 0x80402000U | alpha);
    commands.modes(z_compare | alpha_compare);
    memory.write_halfword(depth_address, {0xfffcU, 3U});
    commands.triangle(9);

    memory.invalidate_banks();
    CHECK(!memory.row_open(depth_address));
    commands.run();
    return memory.row_open(depth_address);
}

} // namespace

TEST(rdp_color_triangle_alpha_rejection_does_not_read_depth) {
    CHECK(!depth_bank_open_after_alpha(0x80U));
}

TEST(rdp_color_triangle_passing_alpha_still_reads_depth) {
    CHECK(depth_bank_open_after_alpha(0xffU));
}
