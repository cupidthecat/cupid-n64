#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>

namespace {
using namespace cupid;
constexpr u64 code = 0xffffffffa0001000ULL;
constexpr u64 delay = 31250000;

void command(System& system, u8 value) {
    system.bus.write(0x1fc007fc, 4, value);
    system.bus.tick(50000);
}

void boot(System& system) {
    command(system, 0x10);
    for (unsigned i = 0; i < 6; ++i)
        system.bus.pif[0x7f2 + i] = static_cast<u8>(system.bus.cic.checksum() >> ((5 - i) * 8));
    command(system, 0x20);
    command(system, 0x40);
    command(system, 8);
}

void prepare(System& system) {
    test::initialize_memory(system);
    boot(system);
    system.cpu.cp0[12] = 0x10000000;
    system.cpu.set_pc(code);
    system.bus.write(0x1000, 4, 0x34020042);
}
} // namespace

TEST(warm_reset_button_asserts_a_maskable_prenmi_interrupt) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[13] & 0x1000U, 0x1000U);
    CHECK_EQ(system.cpu.gpr[2], 0x42U);
    system.cpu.cp0[12] |= 0x1001;
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffff80000180ULL);
    CHECK_EQ(system.cpu.cp0[14], code + 4);
}

TEST(warm_reset_waits_for_the_delay_and_button_release_before_nmi) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.bus.tick(delay);
    CHECK(system.bus.pif_boot.rom_locked());
    system.set_reset_button(false);
    system.bus.tick(1);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
    CHECK_EQ(system.cpu.cp0[30], code);
    CHECK(!system.bus.pif_boot.rom_locked());
}

TEST(warm_reset_restores_the_private_seeds_and_warm_boot_flag) {
    System system;
    prepare(system);
    system.bus.pif[0x7e5] = 0xa0;
    system.bus.pif[0x7e6] = 0x12;
    system.bus.pif[0x7e7] = 0x34;
    system.set_reset_button(true);
    system.set_reset_button(false);
    system.bus.tick(delay);
    CHECK_EQ(system.bus.pif[0x7e5], 0xa6U);
    CHECK_EQ(system.bus.pif[0x7e6], 0x3fU);
    CHECK_EQ(system.bus.pif[0x7e7], 0x3fU);
    CHECK_EQ(system.bus.pif[0x7ff], 0U);
}

TEST(warm_reset_released_button_still_waits_the_full_delay) {
    for (bool cpu_clock : {false, true})
        for (bool split : {false, true}) {
            System system;
            prepare(system);
            system.set_reset_button(true);
            system.set_reset_button(false);
            u64 fraction = 0;
            const auto advance = [&](u64 cycles) {
                if (!cpu_clock) {
                    system.bus.tick(cycles);
                    return;
                }
                const u64 elapsed = (cycles * 3 - fraction + 1) / 2;
                fraction = (elapsed * 2 + fraction) % 3;
                system.advance(elapsed);
            };
            if (split) {
                advance(9999);
                advance(delay - 10000);
            } else
                advance(delay - 1);
            CHECK(system.bus.pif_boot.pre_nmi());
            CHECK(system.bus.pif_boot.rom_locked());
            advance(1);
            CHECK(!system.bus.pif_boot.pre_nmi());
            CHECK(!system.bus.pif_boot.rom_locked());
            system.cpu.step();
            CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
        }
}

TEST(warm_reset_requires_a_new_press_after_boot_termination) {
    System system;
    system.set_reset_button(true);
    prepare(system);
    CHECK(!system.bus.pif_boot.pre_nmi());
    system.set_reset_button(true);
    system.bus.tick(delay);
    CHECK(!system.bus.pif_boot.pre_nmi());
    CHECK(system.bus.pif_boot.rom_locked());
    system.set_reset_button(false);
    system.set_reset_button(true);
    CHECK(system.bus.pif_boot.pre_nmi());
}

TEST(warm_reset_repeated_presses_do_not_extend_the_pending_delay) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.bus.tick(12345);
    system.set_reset_button(true);
    system.set_reset_button(false);
    system.set_reset_button(true);
    system.set_reset_button(false);
    system.bus.tick(delay - 12346);
    CHECK(system.bus.pif_boot.pre_nmi());
    system.bus.tick(1);
    CHECK(!system.bus.pif_boot.pre_nmi());
    CHECK(!system.bus.pif_boot.rom_locked());
}

TEST(warm_reset_cold_reset_cancels_the_sequence_and_preserves_the_button_level) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.bus.tick(delay / 2);
    system.reset();
    CHECK(!system.bus.pif_boot.pre_nmi());
    CHECK_EQ(system.bus.pif[0x7e5], 4U);
    prepare(system);
    system.set_reset_button(true);
    system.bus.tick(delay);
    CHECK(!system.bus.pif_boot.pre_nmi());
    CHECK(system.bus.pif_boot.rom_locked());
    system.set_reset_button(false);
    system.set_reset_button(true);
    CHECK(system.bus.pif_boot.pre_nmi());
}

TEST(warm_reset_prenmi_cannot_be_acknowledged_with_a_cause_write) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.cpu.step();
    system.cpu.write_cop0(13, 0);
    CHECK_EQ(system.cpu.cp0[13] & 0x1000U, 0x1000U);
    system.cpu.cp0[13] = 0;
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[13] & 0x1000U, 0x1000U);
}

TEST(warm_reset_restores_each_security_parts_seeds_and_rearms) {
    constexpr std::array models{CicModel::Nus6101, CicModel::Nus6102, CicModel::Nus7102, CicModel::Nus6103,
                                CicModel::Nus6105, CicModel::Nus6106, CicModel::Nus5101, CicModel::Nus5167,
                                CicModel::Nus8303, CicModel::Nus8401, CicModel::NusDDUS};
    for (const auto model : models)
        for (unsigned ram_mb : {4U, 8U}) {
            System system;
            system.bus.cic.configure(model);
            system.bus.rdram.resize(ram_mb * 1024U * 1024U);
            system.reset();
            prepare(system);
            for (unsigned reset = 0; reset < 2; ++reset) {
                system.set_reset_button(true);
                system.set_reset_button(false);
                system.bus.tick(delay);
                CHECK_EQ(system.bus.pif[0x7e5] & 15U, system.bus.cic.disk_drive() ? 14U : 6U);
                CHECK_EQ(system.bus.pif[0x7e6], system.bus.cic.seed());
                CHECK_EQ(system.bus.pif[0x7e7], system.bus.cic.seed());
                CHECK(!system.bus.pif_boot.failed());
                boot(system);
            }
        }
}

TEST(warm_reset_keeps_ram_hidden_bits_and_device_registers) {
    System system;
    prepare(system);
    auto& bus = system.bus;
    bus.memory.write(0x2000, 8, 0x123456789abcdef0ULL);
    bus.memory.set_hidden_pair(0x2000, 2);
    const u32 chip = bus.memory.read_register(0x03f00000);
    bus.write(0x04700010, 4, 0);
    bus.write(0x04400004, 4, 0x123456);
    bus.write(0x04400034, 4, 0x345);
    system.rsp.memory[27] = 0xab;
    system.rsp.pc = 0x123;
    bus.controller_paks[2][123] = 0x56;
    bus.set_save_type(SaveType::Sram);
    bus.sram[55] = 0x78;
    system.cpu.gpr[8] = 0xabcdef;
    system.set_reset_button(true);
    system.set_reset_button(false);
    bus.tick(delay);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
    CHECK_EQ(system.cpu.gpr[8], 0xabcdefU);
    CHECK_EQ(bus.memory.read(0x2000, 8), 0x123456789abcdef0ULL);
    CHECK_EQ(bus.memory.hidden_pair(0x2000), 2U);
    CHECK_EQ(bus.memory.read_register(0x03f00000), chip);
    CHECK_EQ(bus.read(0x04400004, 4), 0x123456U);
    CHECK_EQ(bus.read(0x04400034, 4), 0x345U);
    CHECK_EQ(system.rsp.memory[27], 0xabU);
    CHECK_EQ(system.rsp.pc, 0x123U);
    CHECK_EQ(bus.controller_paks[2][123], 0x56U);
    CHECK_EQ(bus.sram[55], 0x78U);
}

TEST(warm_reset_keeps_inflight_si_and_its_completion_deadline) {
    System system;
    prepare(system);
    auto& bus = system.bus;
    system.set_reset_button(true);
    system.set_reset_button(false);
    bus.tick(delay - 1000);
    ControllerState controller;
    controller.buttons = 0x8000;
    bus.set_controller_state(0, controller);
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c0] = 1;
    bus.pif[0x7c1] = 4;
    bus.pif[0x7c2] = 1;
    bus.pif[0x7c7] = 0xfe;
    bus.joybus.configure();
    bus.write(0x04800000, 4, 0x2000);
    bus.write(0x04800004, 4, 0x1fc007c0);
    bus.tick(1000);
    system.cpu.step();
    CHECK_EQ(bus.read(0x04800018, 4) & 1U, 1U);
    bus.tick(36019);
    CHECK_EQ(bus.read_ram_byte(0x2003), 0U);
    bus.tick(1);
    CHECK_EQ(bus.read_ram_byte(0x2003), 0x80U);
    CHECK_EQ(bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
}

TEST(warm_reset_keeps_eeprom_programming_active) {
    System system;
    prepare(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Eeprom4K);
    system.set_reset_button(true);
    system.set_reset_button(false);
    bus.tick(delay - 1);
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c4] = 3;
    bus.pif[0x7c5] = 1;
    bus.pif[0x7c6] = 5;
    bus.pif[0x7c8] = 0x12;
    bus.pif[0x7ca] = 0xfe;
    bus.joybus.configure();
    bus.joybus.execute();
    bus.tick(1);
    system.cpu.step();
    const auto status = [&] {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        bus.pif[0x7c4] = 1;
        bus.pif[0x7c5] = 3;
        bus.pif[0x7ca] = 0xfe;
        bus.joybus.configure();
        bus.joybus.execute();
        return bus.pif[0x7c9];
    };
    CHECK_EQ(status(), 0x80U);
    CHECK_EQ(bus.eeprom[0], 0x12U);
    bus.tick(374998);
    CHECK_EQ(status(), 0x80U);
    bus.tick(1);
    CHECK_EQ(status(), 0U);
}

TEST(warm_reset_button_from_an_audio_callback_is_visible_at_the_instruction_boundary) {
    System system;
    prepare(system);
    unsigned callbacks = 0;
    system.bus.audio_output = [&](s16, s16) {
        ++callbacks;
        system.set_reset_button(true);
    };
    system.bus.write(0x04500010, 4, 0);
    system.bus.tick(2000); // Let the next sample boundary latch the new divider.
    system.bus.write(0x04500008, 4, 1);
    system.bus.write(0x04500000, 4, 0x2000);
    system.bus.write(0x04500004, 4, 8);
    system.cpu.step();
    CHECK(callbacks != 0);
    CHECK_EQ(system.cpu.gpr[2], 0x42U);
    CHECK_EQ(system.cpu.pc, code + 4);
    CHECK_EQ(system.cpu.cp0[13] & 0x1000U, 0x1000U);
    CHECK(system.bus.pif_boot.rom_locked());
}

TEST(warm_reset_nmi_is_delivered_even_when_prenmi_is_masked) {
    for (unsigned flags = 0; flags < 8; ++flags) {
        System system;
        prepare(system);
        system.cpu.cp0[12] = 0x10000000U | flags;
        system.set_reset_button(true);
        system.set_reset_button(false);
        system.bus.tick(delay);
        system.cpu.step();
        CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
        CHECK_EQ(system.cpu.cp0[30], code);
        CHECK_EQ(system.cpu.cp0[13] & 0x1000U, 0U);
    }
}

TEST(warm_reset_releases_prenmi_without_acknowledging_other_interrupts) {
    System system;
    prepare(system);
    system.bus.set_interrupt(4, true);
    system.bus.write(0x0430000c, 4, 0x200);
    system.cpu.cp0[13] = 0x8300;
    system.set_reset_button(true);
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[13] & 0xff00U, 0x9700U);
    system.set_reset_button(false);
    system.bus.tick(delay);
    system.cpu.step();
    CHECK_EQ(system.cpu.cp0[13] & 0xff00U, 0x8700U);
}

TEST(warm_reset_refreshes_the_acknowledgement_while_waiting_for_cic) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.bus.pif[0x7ff] = 0;
    system.bus.tick(1);
    CHECK_EQ(system.bus.pif[0x7ff], 0x80U);
    system.set_reset_button(false);
    system.bus.tick(delay - 1);
    CHECK_EQ(system.bus.pif[0x7ff], 0U);
}

TEST(warm_reset_keeps_the_rsp_executing_across_nmi) {
    System system;
    prepare(system);
    system.set_reset_button(true);
    system.set_reset_button(false);
    system.bus.tick(delay - 10);
    write_be32(system.rsp.memory.data() + 0x1000, 0x25080001); // ADDIU t0,t0,1
    write_be32(system.rsp.memory.data() + 0x1004, 0xac080000); // SW t0,0(zero)
    write_be32(system.rsp.memory.data() + 0x1008, 0x08000000); // J 0
    write_be32(system.rsp.memory.data() + 0x100c, 0);
    system.bus.write(0x04040010, 4, 1);
    system.advance(15);
    CHECK(!system.bus.pif_boot.pre_nmi());
    const u32 before = read_be32(system.rsp.memory.data());
    CHECK(before > 0);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
    CHECK(system.rsp.running());
    system.advance(12);
    CHECK(read_be32(system.rsp.memory.data()) > before);
}
