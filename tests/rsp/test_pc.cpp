#include "test.hpp"

#include "cupid/system.hpp"

#include <memory>

using namespace cupid;

namespace {

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(&rsp.memory[0x1000U | (address & 0xffcU)], word);
}

void pending_branch(System& system, u32 start = 0) {
    instruction(system.rsp, start, 0x08000008U);      // J 0x20
    instruction(system.rsp, start + 4, 0x24011234U);  // ADDIU r1, r0, 0x1234
    instruction(system.rsp, start + 8, 0xac010000U);  // SW r1, 0(r0)
    instruction(system.rsp, start + 12, 0x0000000dU); // BREAK
    instruction(system.rsp, 0x20, 0x0000000dU);
    system.bus.write(0x04080000U, 4, start);
    system.rsp.write_register(0x10, 0x41U); // Clear halt, enable single step.
    system.rsp.tick(1);
    CHECK_EQ(system.bus.read(0x04080000U, 4), (start + 4) & 0xffcU);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 1U);
}

void resume(System& system) {
    system.rsp.write_register(0x10, 0x21U); // Clear halt and single step.
    system.rsp.tick(8);
    CHECK_EQ(system.rsp.read_register(0x10) & 3U, 3U);
}

} // namespace

TEST(rsp_pc_same_address_write_discards_pending_branch) {
    for (const u32 address : {0x04080000U, 0x040bffe0U}) {
        for (const u32 value : {4U, 0xfffff007U}) {
            auto system = std::make_unique<System>();
            pending_branch(*system);
            system->bus.write(address, 4, value);
            CHECK_EQ(system->rsp.read_register(0x10) & 0x23U, 0x21U);
            resume(*system);
            CHECK_EQ(read_be32(system->rsp.memory.data()), 0x1234U);
            CHECK_EQ(system->bus.read(0x04080000U, 4), 0x10U);
        }
    }
}

TEST(rsp_pc_read_and_halt_resume_preserve_pending_branch) {
    auto system = std::make_unique<System>();
    pending_branch(*system);
    CHECK_EQ(system->bus.read(0x040bffe0U, 4), 4U);
    resume(*system);
    CHECK_EQ(read_be32(system->rsp.memory.data()), 0U);
    CHECK_EQ(system->bus.read(0x04080000U, 4), 0x24U);
}

TEST(rsp_pc_same_address_write_at_imem_wrap_discards_pending_branch) {
    auto system = std::make_unique<System>();
    pending_branch(*system, 0xffcU);
    system->bus.write(0x04080000U, 4, 0);
    resume(*system);
    CHECK_EQ(read_be32(system->rsp.memory.data()), 0x1234U);
    CHECK_EQ(system->bus.read(0x04080000U, 4), 0x0cU);
}

TEST(rsp_pc_write_while_running_restarts_sequencing) {
    auto system = std::make_unique<System>();
    pending_branch(*system);
    system->rsp.write_register(0x10, 0x21U);
    CHECK(system->rsp.running());
    system->bus.write(0x04080000U, 4, 4);
    CHECK(system->rsp.running());
    system->rsp.tick(8);
    CHECK_EQ(read_be32(system->rsp.memory.data()), 0x1234U);
    CHECK_EQ(system->bus.read(0x04080000U, 4), 0x10U);
}
