#include "cupid/system.hpp"
#include "test.hpp"

#include <array>
#include <memory>

using namespace cupid;

namespace {

void prepare(System& system, u32 start = 0) {
    constexpr std::array<u32, 16> program{
        0x8c010100U, // LW at,0x100(zero).
        0x00211021U, // ADDU v0,at,at.
        0xac020100U, // SW v0,0x100(zero).
        0xc8012010U, // LQV v1,0x100(zero).
        0x4a010850U, // VADD v1,v1,v1.
        0xe8012011U, // SQV v1,0x110(zero).
        0x24840001U, // ADDIU a0,a0,1.
        0x34850055U, // ORI a1,a0,0x55.
        0xa405000fU, // SH a1,0xf(zero).
        0x24060fffU, // ADDIU a2,zero,0xfff.
        0xe8c10800U, // SSV v1[0],0(a2).
        0x8c070110U, // LW a3,0x110(zero).
        0x00e44026U, // XOR t0,a3,a0.
        0xac080120U, // SW t0,0x120(zero).
        0x1000fff1U, // BEQ zero,zero,0.
        0U,
    };
    for (unsigned index = 0; index < program.size(); ++index)
        system.bus.write(0x04001000U + ((start + index * 4U) & 0x0fffU), 4, program[index]);
    for (u32 offset = 0; offset < 16; offset += 4)
        system.bus.write(0x04000100U + offset, 4, 0x12345678U + offset);
    system.rsp.write_pc(start);
    system.rsp.write_register(0x10, 1U);
}

void compare(System& batched, System& stepped, u64 cycles) {
    batched.advance(cycles);
    for (u64 cycle = 0; cycle < cycles; ++cycle)
        stepped.advance(1);
    CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
    CHECK_EQ(batched.rsp.read_register(0x10), stepped.rsp.read_register(0x10));
    for (u32 address = 0; address < 4096; address += 4)
        CHECK_EQ(batched.bus.read(0x04000000U + address, 4), stepped.bus.read(0x04000000U + address, 4));
    CHECK(batched.rsp.memory.imem_trusted());
    CHECK(stepped.rsp.memory.imem_trusted());
}

} // namespace

TEST(rsp_local_blocks_preserve_partial_stalls_pairing_and_wrapped_stores) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [batched, stepped] = *machines;
        for (auto* system : {&batched, &stepped}) {
            system->advance(phase);
            prepare(*system);
        }
        for (const u64 cycles : {384U, 1U, 2U, 3U, 16U, 63U, 96U, 127U, 1024U, 17U})
            compare(batched, stepped, cycles);
    }
}

TEST(rsp_local_blocks_revalidate_an_interior_imem_write) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped})
        prepare(*system);
    compare(batched, stepped, 384);
    for (auto* system : {&batched, &stepped}) {
        system->rsp.write_pc(0);
        system->bus.write(0x0400101cU, 4, 0x348500aaU); // ORI a1,a0,0xaa; entry word is unchanged.
    }
    compare(batched, stepped, 384);
    CHECK_EQ(batched.bus.read(0x0400000fU, 2) & 0xaaU, 0xaaU);
}

TEST(rsp_local_blocks_cross_imem_wrap_at_partial_boundaries) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped})
        prepare(*system, 0xfe0U);
    for (const u64 cycles : {384U, 1U, 127U, 1024U})
        compare(batched, stepped, cycles);
}

TEST(rsp_local_blocks_include_incoming_operand_history) {
    constexpr std::array<u64, 3> budgets{96U, 95U, 93U};
    for (unsigned age = 0; age < budgets.size(); ++age) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [batched, stepped] = *machines;
        for (auto* system : {&batched, &stepped}) {
            for (u32 offset = 0; offset < 64; offset += 4)
                system->bus.write(0x04001000U + offset, 4, 0x4a010850U); // VADD v1,v1,v1.
            system->bus.write(0x04001040U, 4, 0x4a000037U); // VNOP separates the final VADD from BREAK.
            system->bus.write(0x04001044U, 4, 0x0000000dU);
            system->bus.write(0x04001200U, 4, 0xc8012010U); // LQV v1,0x100(zero).
            system->rsp.write_register(0x10, 1U);
        }
        compare(batched, stepped, 192); // Cache the block with no incoming vector dependency.
        CHECK(!batched.rsp.running());
        for (auto* system : {&batched, &stepped}) {
            system->rsp.write_pc(0x200U);
            system->rsp.write_register(0x10, 1U);
            system->rsp.tick(1U + age);
            system->rsp.write_pc(0);
        }
        // Stop on the last VADD's issue cycle. Reusing the earlier block cost
        // would incorrectly execute BREAK before this observation.
        compare(batched, stepped, budgets[age]);
        CHECK(batched.rsp.running());
        CHECK_EQ(batched.rsp.pc, 0x40U);
        compare(batched, stepped, 8);
        CHECK(!batched.rsp.running());
    }
}

TEST(rsp_local_blocks_preserve_branch_target_single_issue_restriction) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [batched, stepped] = *machines;
    for (auto* system : {&batched, &stepped}) {
        system->bus.write(0x04001004U, 4, 0x4a010850U); // VADD v1,v1,v1.
        system->bus.write(0x04001008U, 4, 0x24010001U); // ADDIU at,zero,1; eligible partner.
        system->bus.write(0x0400100cU, 4, 0x8c020100U); // LW v0,0x100(zero).
        system->bus.write(0x04001010U, 4, 0x00421821U); // ADDU v1,v0,v0; two operand bubbles.
        system->bus.write(0x04001044U, 4, 0x0000000dU); // BREAK after sixteen instructions.
        system->bus.write(0x04001200U, 4, 0x1000ff80U); // BEQ zero,zero,0x04.
        system->rsp.write_pc(4U);
        system->rsp.write_register(0x10, 1U);
    }
    compare(batched, stepped, 192); // Cache the entry with pairing enabled.
    CHECK(!batched.rsp.running());
    for (auto* system : {&batched, &stepped}) {
        system->rsp.write_pc(0x200U);
        system->rsp.write_register(0x10, 1U);
        system->rsp.tick(3); // Taken branch, delay slot, bubble; the unaligned target issues alone.
    }
    compare(batched, stepped, 27);
    CHECK(batched.rsp.running());
    CHECK_EQ(batched.rsp.pc, 0x44U);
    compare(batched, stepped, 8);
    CHECK(!batched.rsp.running());
}
