#include "cupid/rsp/pipeline.hpp"
#include "cupid/system.hpp"
#include "test.hpp"

#include <array>

using namespace cupid;

TEST(rsp_bulk_operand_bubbles_match_every_partial_cycle_and_following_packet) {
    // Scalar/vector loads and consumers exercise both result latencies and the
    // load/store interlock. Paired packets can carry both dependency classes.
    constexpr std::array<u32, 9> words{0U,          0x8c010000U, 0xac010000U, 0x00211021U, 0xc8012000U,
                                       0xe8012000U, 0x4a010850U, 0x4a000037U, 0xc8022000U};
    u32 random = 0x9e3779b9U;
    const auto next = [&] {
        random = random * 1664525U + 1013904223U;
        return words[(random >> 16U) % words.size()];
    };
    RspPipeline seed;
    for (unsigned sequence = 0; sequence < 2048; ++sequence) {
        seed.fetch(next(), next(), false);
        for (unsigned budget = 0; budget <= 4; ++budget) {
            auto stepped = seed;
            auto batched = seed;
            unsigned expected = 0;
            while (expected < budget && stepped.advance_operand_wait())
                ++expected;
            CHECK_EQ(batched.advance_operand_wait(budget), expected);
            for (unsigned cycle = 0; cycle < 4; ++cycle)
                CHECK_EQ(batched.advance_operand_wait(), stepped.advance_operand_wait());
            stepped.retire(false, 0);
            batched.retire(false, 0);
            // The next packet checks the stage contents left after a bulk shift.
            for (const u32 word : words) {
                auto next_stepped = stepped;
                auto next_batched = batched;
                next_stepped.fetch(word, 0, false);
                next_batched.fetch(word, 0, false);
                for (unsigned cycle = 0; cycle < 4; ++cycle)
                    CHECK_EQ(next_batched.advance_operand_wait(), next_stepped.advance_operand_wait());
            }
        }
        while (seed.advance_operand_wait()) {
        }
        seed.retire(false, 0);
    }
}

TEST(rsp_local_bulk_stalls_preserve_state_at_every_slice_boundary) {
    constexpr std::array<u32, 10> program{
        0x8c010000U, // LW at,0(zero).
        0x00211021U, // ADDU v0,at,at.
        0xac020004U, // SW v0,4(zero).
        0xc8012000U, // LQV v1,0(zero).
        0x4a010850U, // VADD v1,v1,v1.
        0xe8012001U, // SQV v1,16(zero).
        0x24210001U, // ADDIU at,at,1.
        0xac010000U, // SW at,0(zero).
        0x1000fff7U, // BEQ zero,zero,0.
        0U,
    };
    for (unsigned budget = 1; budget <= 32; ++budget) {
        System stepped, batched;
        for (auto* system : {&stepped, &batched}) {
            for (unsigned index = 0; index < program.size(); ++index)
                system->bus.write(0x04001000U + index * 4U, 4, program[index]);
            system->bus.write(0x04000000U, 4, 1);
            system->rsp.write_register(0x10, 1);
        }
        for (unsigned slice = 0; slice < 64; ++slice) {
            batched.advance(budget);
            for (unsigned cycle = 0; cycle < budget; ++cycle)
                stepped.advance(1);
            CHECK_EQ(batched.rsp.pc, stepped.rsp.pc);
            // Bus reads retain trusted IMEM storage for the next local slice.
            for (u32 address = 0; address < 32; address += 4)
                CHECK_EQ(batched.bus.read(0x04000000U + address, 4),
                         stepped.bus.read(0x04000000U + address, 4));
            CHECK(batched.rsp.memory.imem_trusted());
        }
    }
}
