#include "test.hpp"

#include "cupid/system.hpp"

#include <array>
#include <memory>

using namespace cupid;

namespace {

constexpr u32 regimm(unsigned rs, unsigned kind, s16 immediate) {
    return (0x01U << 26U) | (rs << 21U) | (kind << 16U) | static_cast<u16>(immediate);
}

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(rsp.memory.data() + 0x1000U + address, word);
}

struct LinkBranchResult {
    u32 link;
    u32 delay_value;
    u32 fallthrough_value;
};

LinkBranchResult run_link_branch(bool negative, unsigned kind) {
    auto system = std::make_unique<System>();
    constexpr u32 lui_ra_negative = 0x3c1f8000U;
    constexpr u32 addiu_ra_positive = 0x241f0001U;
    constexpr u32 delay_value = 0x24010011U;
    constexpr u32 fallthrough_value = 0x24020022U;
    constexpr u32 store_ra = 0xac1f0000U;
    constexpr u32 store_delay = 0xac010004U;
    constexpr u32 store_fallthrough = 0xac020008U;
    constexpr u32 break_instruction = 0x0000000dU;

    const std::array<u32, 8> program{
        negative ? lui_ra_negative : addiu_ra_positive,
        regimm(31, kind, 2), // Target 0x10; link value 0x0c.
        delay_value,
        fallthrough_value,
        store_ra,
        store_delay,
        store_fallthrough,
        break_instruction,
    };
    u32 address = 0;
    for (const u32 word : program) {
        instruction(system->rsp, address, word);
        address += 4U;
    }

    system->rsp.write_register(0x10, 1U);
    system->advance(64);
    CHECK_EQ(system->rsp.read_register(0x10) & 3U, 3U);
    return {
        read_be32(system->rsp.memory.data()),
        read_be32(system->rsp.memory.data() + 4U),
        read_be32(system->rsp.memory.data() + 8U),
    };
}

} // namespace

TEST(rsp_decoded_regimm_link_branches_snapshot_rs31_before_link_write) {
    constexpr unsigned bltzal = 0x10U;
    constexpr unsigned bgezal = 0x11U;

    for (const bool negative : {false, true}) {
        const auto bltz = run_link_branch(negative, bltzal);
        CHECK_EQ(bltz.link, 0x0cU);
        CHECK_EQ(bltz.delay_value, 0x11U);
        CHECK_EQ(bltz.fallthrough_value, negative ? 0U : 0x22U);

        const auto bgez = run_link_branch(negative, bgezal);
        CHECK_EQ(bgez.link, 0x0cU);
        CHECK_EQ(bgez.delay_value, 0x11U);
        CHECK_EQ(bgez.fallthrough_value, negative ? 0x22U : 0U);
    }
}

TEST(rsp_decoded_scalar_memory_and_branches) {
    auto system = std::make_unique<System>();
    write_be32(system->rsp.memory.data(), 0x89abcdefU);
    write_be32(system->rsp.memory.data() + 4U, 0x01234567U);

    const std::array<u32, 9> program{
        0x8c080000U, // LW  $t0, 0($zero)
        0x80090000U, // LB  $t1, 0($zero)
        0x900a0000U, // LBU $t2, 0($zero)
        0x840b0000U, // LH  $t3, 0($zero)
        0x940c0000U, // LHU $t4, 0($zero)
        0xac080020U, // SW  $t0, 0x20($zero)
        0xa40c0024U, // SH  $t4, 0x24($zero)
        0xa00a0026U, // SB  $t2, 0x26($zero)
        0x0000000dU, // BREAK
    };
    u32 address = 0;
    for (const u32 word : program) {
        instruction(system->rsp, address, word);
        address += 4U;
    }
    system->rsp.write_register(0x10, 1U);
    system->advance(64);
    CHECK_EQ(system->rsp.read_register(0x10) & 3U, 3U);
    CHECK_EQ(read_be32(system->rsp.memory.data() + 0x20U), 0x89abcdefU);
    CHECK_EQ(read_be16(system->rsp.memory.data() + 0x24U), 0x89abU);
    CHECK_EQ(system->rsp.memory[0x26U], 0x89U);
}
