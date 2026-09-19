#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

using namespace cupid;

TEST(cpu_ctc1_exception_samples_the_following_coprocessor_decode) {
    for (unsigned coprocessor : {0U, 1U, 2U}) {
        System system;
        test::initialize_memory(system);
        auto& cpu = system.cpu;
        cpu.write_cop0(12, 0x74000000);
        cpu.set_pc(0xffffffff80001000ULL);
        cpu.gpr[2] = 0x4200;
        system.bus.write(0x1000, 4, 0x44c2f800); // CTC1 v0, FCSR
        cpu.gpr[3] = 0x12345678;
        cpu.fpu.registers[0] = 0x87654321;
        cpu.cop2_latch = 0xfeedbeef;
        system.bus.write(0x1004, 4, 0x40030000U | (coprocessor << 26));
        cpu.step();
        CHECK(cpu.exception_pending);
        CHECK_EQ(cpu.read_cop0(14), 0xffffffff80001000ULL);
        CHECK_EQ(cpu.read_cop0(13) & 0x3000007cU, (coprocessor << 28) | 0x3cU);
        CHECK_EQ(cpu.fpu.control, 0x4200U);
        CHECK_EQ(cpu.gpr[3], 0x12345678U);
    }
}

TEST(cpu_fpu_arithmetic_exception_samples_decode_without_executing_it) {
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x34000000);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.fpu.registers[2] = 1; // A subnormal source raises an unimplemented-operation exception.
    cpu.fpu.registers[4] = 0x3f800000;
    cpu.fpu.registers[6] = 0x12345678;
    system.bus.write(0x1000, 4, 0x46041180); // ADD.S f6, f2, f4
    system.bus.write(0x1004, 4, 0x44843000); // MTC1 a0, f6
    cpu.step();
    CHECK(cpu.exception_pending);
    CHECK_EQ(cpu.read_cop0(13) & 0x3000007cU, 0x1000003cU);
    CHECK_EQ(cpu.read_cop0(14), 0xffffffff80001000ULL);
    CHECK_EQ(cpu.fpu.registers[6], 0x12345678U);
}

TEST(cpu_fpu_delay_slot_exception_samples_the_branch_target) {
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x74000000);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.gpr[2] = 0x4200;
    system.bus.write(0x1000, 4, 0x10000007); // BEQ zero, zero, 0x1020
    system.bus.write(0x1004, 4, 0x44c2f800);
    system.bus.write(0x1008, 4, 0x44000000); // Sequential decode differs from the target.
    system.bus.write(0x1020, 4, 0x48000000);
    cpu.step();
    cpu.step();
    CHECK(cpu.exception_pending);
    CHECK_EQ(cpu.read_cop0(14), 0xffffffff80001000ULL);
    CHECK_EQ(cpu.read_cop0(13) & 0xb000007cU, 0xa000003cU);
}

TEST(cpu_fpu_exception_decode_uses_cached_instruction_bytes) {
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x74000000);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.gpr[2] = 0x4200;
    system.bus.write(0x1000, 4, 0x44c2f800);
    system.bus.write(0x1004, 4, 0x48000000);
    u64 instruction = 0;
    CHECK(cpu.read_memory(cpu.pc, 4, instruction, true));
    system.bus.write(0x1004, 4, 0x44000000);
    cpu.step();
    CHECK_EQ(cpu.read_cop0(13) & 0x3000007cU, 0x2000003cU);
}

TEST(cpu_fpu_exception_discards_younger_fetch_faults) {
    for (u64 target : {0x4000ULL, 0xffffffff80001001ULL, 0x0000010000000000ULL, 0xffffffffa4900000ULL}) {
        System system;
        test::initialize_memory(system);
        auto& cpu = system.cpu;
        cpu.write_cop0(12, 0x74000000);
        cpu.set_pc(0xffffffff80001000ULL);
        cpu.gpr[2] = 0x4200;
        cpu.gpr[3] = target;
        cpu.cp0[8] = 0x12345678;
        cpu.cp0[4] = 0x12300000;
        cpu.cp0[10] = 0x32100000;
        cpu.cp0[20] = 0x45600000;
        system.bus.write(0x1000, 4, 0x00600008); // JR v1
        system.bus.write(0x1004, 4, 0x44c2f800);
        cpu.step();
        cpu.step();
        CHECK(cpu.exception_pending);
        CHECK(!cpu.frozen);
        CHECK_EQ(cpu.pc, 0xffffffff80000180ULL);
        CHECK_EQ(cpu.read_cop0(14), 0xffffffff80001000ULL);
        CHECK_EQ(cpu.read_cop0(13) & 0xb000007cU, 0x8000003cU);
        CHECK_EQ(cpu.cp0[8], 0x12345678U);
        CHECK_EQ(cpu.cp0[4], 0x12300000U);
        CHECK_EQ(cpu.cp0[10], 0x32100000U);
        CHECK_EQ(cpu.cp0[20], 0x45600000U);
        cpu.set_pc(target);
        cpu.step();
        CHECK(cpu.exception_pending || cpu.frozen);
    }
}

TEST(cpu_fpu_exception_clears_stale_coprocessor_cause_on_integer_decode) {
    System system;
    test::initialize_memory(system);
    auto& cpu = system.cpu;
    cpu.write_cop0(12, 0x34000000);
    cpu.set_pc(0xffffffff80001000ULL);
    cpu.cp0[13] = 0x20000000;
    cpu.gpr[2] = 0x4200;
    system.bus.write(0x1000, 4, 0x44c2f800);
    system.bus.write(0x1004, 4, 0x34030042); // ORI v1, zero, 0x42
    cpu.step();
    CHECK_EQ(cpu.read_cop0(13) & 0x3000007cU, 0x3cU);
    CHECK_EQ(cpu.gpr[3], 0U);
}
