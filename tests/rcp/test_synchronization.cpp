#include "cupid/system.hpp"
#include "test.hpp"

#include <array>

namespace {
using namespace cupid;

void clock_program(System& system) {
    constexpr std::array<u32, 8> program = {0x40016000, 0,          0x00031940, 0,
                                            0x40026000, 0xac010000, 0xac020004, 0x0000000d};
    for (u32 index = 0; index < program.size(); ++index) {
        system.bus.write(0x04001000U + index * 4, 4, program[index]);
    }
    system.rsp.write_register(0x10, 1);
}
} // namespace

TEST(rcp_rsp_reads_dp_clock_at_each_instruction_boundary) {
    System system;
    clock_program(system);
    system.advance(12);
    const u64 before = system.bus.read(0x04000000, 4);
    const u64 after = system.bus.read(0x04000004, 4);
    CHECK_EQ(after - before, 4U);
    CHECK((system.rsp.read_register(0x10) & 3U) == 3U);
}

TEST(rcp_clock_reads_are_independent_of_cpu_batch_size) {
    System single;
    System split;
    clock_program(single);
    clock_program(split);
    single.advance(15);
    for (unsigned cycle = 0; cycle < 15; ++cycle)
        split.advance(1);
    CHECK_EQ(single.bus.read(0x04000000, 4), split.bus.read(0x04000000, 4));
    CHECK_EQ(single.bus.read(0x04000004, 4), split.bus.read(0x04000004, 4));
    CHECK_EQ(single.bus.read(0x04100010, 4), split.bus.read(0x04100010, 4));
}
