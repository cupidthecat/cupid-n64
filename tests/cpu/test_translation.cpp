#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffffa0001000ULL;
constexpr u64 misaligned_address = 0x00400004ULL;
constexpr u64 destination_sentinel = 0x0123456789abcdefULL;
constexpr u32 lladdr_sentinel = 0x0badf00dU;

constexpr u32 immediate(unsigned opcode, unsigned rs, unsigned rt, u16 value) {
    return (opcode << 26) | (rs << 21) | (rt << 16) | value;
}

u32 exception_code(const Cpu& cpu) {
    return static_cast<u32>((cpu.cp0[13] >> 2) & 31U);
}

void map(Cpu& cpu, u32 flags) {
    cpu.write_cop0(2, (0x2000U >> 6) | flags);
    cpu.write_cop0(3, (0x3000U >> 6) | flags);
    cpu.write_cop0(5, 0);
    cpu.write_cop0(10, misaligned_address & ~0x1fffULL);
    cpu.tlb_write(0);
}

void prepare(System& system) {
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x34000000U);
    cpu.set_pc(code);
    cpu.gpr[1] = misaligned_address;
    cpu.gpr[2] = destination_sentinel;
    cpu.write_cop0(17, lladdr_sentinel);
    cpu.linked = true;
    system.bus.write(0x1000, 4, 0x10000001U);              // BEQ zero, zero, +1
    system.bus.write(0x1004, 4, immediate(0x34, 1, 2, 0)); // LLD v0, 0(at)
}

void expect_address_error(System& system) {
    auto& cpu = system.cpu;
    cpu.step();
    CHECK(!cpu.exception_pending);
    CHECK_EQ(cpu.pc, code + 4);

    cpu.step();
    CHECK(cpu.exception_pending);
    CHECK_EQ(exception_code(cpu), 4U);
    CHECK_EQ(cpu.cp0[8], misaligned_address);
    CHECK_EQ(cpu.cp0[14], code);
    CHECK_EQ(cpu.cp0[13] & 0x8000007cULL, 0x80000010ULL);
    CHECK_EQ(cpu.gpr[2], destination_sentinel);
    CHECK_EQ(cpu.cp0[17], lladdr_sentinel);
    CHECK(cpu.linked);
}
} // namespace

TEST(cpu_lld_alignment_precedes_missing_tlb_mapping) {
    System system;
    prepare(system);
    expect_address_error(system);
}

TEST(cpu_lld_alignment_precedes_invalid_tlb_mapping) {
    System system;
    prepare(system);
    map(system.cpu, 0x15U); // global, dirty, uncached, invalid
    expect_address_error(system);
}

TEST(cpu_lld_alignment_fault_is_unchanged_by_valid_tlb_mapping) {
    System system;
    prepare(system);
    map(system.cpu, 0x17U); // global, dirty, valid, uncached
    expect_address_error(system);
}
