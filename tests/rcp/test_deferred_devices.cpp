#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <string>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 code = 0xffffffff80001000ULL;

void load_program(System& system, std::span<const u32> program, u64 address = code) {
    const auto physical = static_cast<u32>(address) & 0x1fffffffU;
    for (u32 index = 0; index < program.size(); ++index)
        system.bus.write(physical + index * 4, 4, program[index]);
    system.cpu.set_pc(address);
}

// A cached loop that keeps storing to RDRAM while VI lines, a PI DMA and an SP DMA
// run on the devices. Interrupts stay masked in Status so Cause records their arrival.
void busy_machine(System& system) {
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    std::vector<u8> rom(0x4000, 0);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    for (std::size_t index = 0x1000; index < rom.size(); ++index)
        rom[index] = static_cast<u8>(index * 7);
    std::string error;
    CHECK(system.bus.load_rom(rom, error));

    auto& bus = system.bus;
    bus.write(0x0430000c, 4, 0xaaa); // unmask every MI interrupt
    bus.write(0x04400000, 4, 0x303);
    bus.write(0x04400004, 4, 0x3000);
    bus.write(0x04400008, 4, 16);
    bus.write(0x0440000c, 4, 4);
    bus.write(0x04400018, 4, 13);
    bus.write(0x0440001c, 4, 99);
    bus.write(0x04400020, 4, (100U << 16) | 100U);
    bus.write(0x04400024, 4, (108U << 16) | 111U);
    bus.write(0x04400028, 4, (2U << 16) | 4U);
    bus.write(0x04400030, 4, 1024);
    bus.write(0x04400034, 4, 1024);
    bus.write(0x04600000, 4, 0x2000);
    bus.write(0x04600004, 4, 0x10001000);
    bus.write(0x0460000c, 4, 0x7ff);
    bus.write(0x04040000, 4, 0x1000);
    bus.write(0x04040004, 4, 0x3000);
    bus.write(0x04040008, 4, 0x3ff);

    constexpr std::array<u32, 5> program = {
        0x3c048000, // LUI a0, 0x8000
        0x24420001, // ADDIU v0, v0, 1
        0xac820100, // SW v0, 0x100(a0)
        0x1000fffd, // B -3
        0x00000000, // NOP
    };
    load_program(system, program);
}

void compare_state(System& deferred, System& eager) {
    CHECK_EQ(deferred.cpu.cycles, eager.cpu.cycles);
    CHECK_EQ(deferred.cpu.pc, eager.cpu.pc);
    CHECK(deferred.cpu.cp0 == eager.cpu.cp0);
    CHECK_EQ(deferred.bus.output_clock(), eager.bus.output_clock());
    CHECK_EQ(deferred.bus.rdram_refresh_wait(), eager.bus.rdram_refresh_wait());
    for (u32 address : {0x04300008U, 0x04400010U, 0x04600010U, 0x04040010U, 0x04040018U, 0x0410000cU})
        CHECK_EQ(deferred.bus.read(address, 4), eager.bus.read(address, 4));
    CHECK(deferred.rsp.memory == eager.rsp.memory);
    CHECK(deferred.bus.rdram == eager.bus.rdram);
}

} // namespace

TEST(rcp_deferred_device_time_matches_eager_advance_after_every_step) {
    System deferred, eager;
    busy_machine(deferred);
    busy_machine(eager);
    for (unsigned step = 0; step < 6000; ++step) {
        deferred.cpu.step();
        eager.cpu.step();
        // A zero-length public advance brings the devices up to the CPU immediately.
        eager.advance(0);
        if (step % 97 == 0)
            compare_state(deferred, eager);
    }
    compare_state(deferred, eager);
    CHECK((deferred.cpu.cp0[13] & 0x400U) != 0);
}

TEST(rcp_deferred_device_time_keeps_cart_write_latch_visible_to_a_prompt_read) {
    System system;
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000U);
    // A long cached countdown first lets the devices fall well behind the CPU.
    constexpr std::array<u32, 15> program = {
        0x3c04badc, // LUI a0, 0xbadc
        0x34840ffe, // ORI a0, a0, 0x0ffe
        0x3c02b000, // LUI v0, 0xb000
        0x340501f4, // ORI a1, zero, 500
        0x20a5ffff, // ADDI a1, a1, -1
        0x1ca0fffe, // BGTZ a1, -2
        0x00000000, // NOP
        0xac440000, // SW a0, 0(v0)
        0x3405000a, // ORI a1, zero, 10
        0x20a5ffff, // ADDI a1, a1, -1
        0x1ca0fffe, // BGTZ a1, -2
        0x00000000, // NOP
        0x8c430000, // LW v1, 0(v0)
        0x1000ffff, // B 0
        0x00000000, // NOP
    };
    load_program(system, program);
    while (system.cpu.pc != code + 52 && system.cpu.instruction_count < 4000)
        system.cpu.step();
    CHECK_EQ(system.cpu.pc, code + 52);
    // The write buffer count started when the store issued, so the PI bus is still
    // busy and returns the stored word.
    CHECK_EQ(system.cpu.gpr[3], 0xffffffffbadc0ffeULL);
}
