#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace {
using namespace cupid;

struct Clock {
    System& system;
    bool cpu;
    u64 fraction{};
    void operator()(u64 cycles) {
        if (!cpu) {
            system.bus.tick(cycles);
            return;
        }
        const u64 elapsed = (cycles * 3 - fraction + 1) / 2;
        fraction = (elapsed * 2 + fraction) % 3;
        system.advance(elapsed);
    }
};

void command(System& system, u8 value) {
    system.bus.write(0x1fc007fc, 4, value);
    system.bus.tick(50000);
}

void checksum(System& system, bool valid) {
    command(system, 0x10);
    const u64 value = system.bus.cic.checksum() ^ (valid ? 0U : 1U);
    for (unsigned i = 0; i < 6; ++i)
        system.bus.pif[0x7f2 + i] = static_cast<u8>(value >> ((5 - i) * 8));
    command(system, 0x20);
    command(system, 0x40);
}
} // namespace

TEST(pif_boot_checksum_capture_waits_for_rom_lockout) {
    System system;
    command(system, 0x20);
    CHECK_EQ(system.bus.pif[0x7ff], 0x20U);
    CHECK_EQ(system.bus.pif[0x7e6], system.bus.cic.seed());
}

TEST(pif_boot_bad_checksum_requests_nmi) {
    System system;
    test::initialize_memory(system);
    checksum(system, false);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
    CHECK_EQ(system.cpu.cp0[30], 0xffffffff80001000ULL);
}

TEST(pif_boot_termination_waits_for_checksum_validation) {
    System system;
    command(system, 0x08);
    CHECK_EQ(system.bus.pif[0x7ff], 0x08U);
}

TEST(pif_boot_lockout_and_capture_occur_at_separate_clock_boundaries) {
    for (bool cpu_clock : {false, true}) {
        System system;
        auto& bus = system.bus;
        Clock advance{system, cpu_clock};
        bus.pif[0] = 0xab;
        bus.write(0x1fc007fc, 4, 0x30);
        advance(27306);
        CHECK_EQ(bus.read(0x1fc00000, 1), 0xabU);
        CHECK_EQ(bus.pif[0x7e6], 0x3fU);
        advance(1);
        CHECK_EQ(bus.read(0x1fc00000, 1), 0U);
        CHECK_EQ(bus.pif[0x7ff], 0x30U);
        advance(27306);
        CHECK_EQ(bus.pif[0x7ff], 0x30U);
        advance(1);
        CHECK_EQ(bus.pif[0x7ff], 0xb0U);
        CHECK_EQ(bus.pif[0x7e6], 0U);
    }
}

TEST(pif_boot_validates_every_configured_security_part_and_ram_size) {
    constexpr std::array models{CicModel::Nus6101, CicModel::Nus6102, CicModel::Nus7102, CicModel::Nus6103,
                                CicModel::Nus6105, CicModel::Nus6106, CicModel::Nus5101, CicModel::Nus5167,
                                CicModel::Nus8303, CicModel::Nus8401, CicModel::NusDDUS};
    for (const auto model : models)
        for (unsigned ram_mb : {4U, 8U}) {
            System system;
            auto& bus = system.bus;
            bus.cic.configure(model);
            bus.rdram.resize(ram_mb * 1024U * 1024U);
            system.reset();
            CHECK_EQ(bus.pif[0x7e5], bus.cic.disk_drive() ? 12U : 4U);
            CHECK_EQ(bus.pif[0x7e6], bus.cic.seed());
            CHECK_EQ(bus.pif[0x7e7], bus.cic.seed());
            checksum(system, true);
            CHECK(!bus.pif_boot.failed());
            CHECK_EQ(bus.pif[0x7ff], 0U);
            command(system, 8);
            CHECK_EQ(bus.pif[0x7ff], 0U);
            CHECK_EQ(bus.pif_boot.next_event(), std::numeric_limits<u64>::max());
            bus.tick(400000000);
            CHECK(!bus.pif_boot.failed());
        }
}

TEST(pif_boot_captures_secrets_once_and_preserves_adjacent_nibbles) {
    System system;
    auto& bus = system.bus;
    command(system, 0x10);
    bus.pif[0x7e5] = 0xa4;
    for (unsigned i = 0; i < 6; ++i)
        bus.pif[0x7f2 + i] = static_cast<u8>(bus.cic.checksum() >> ((5 - i) * 8));
    command(system, 0x20);
    CHECK_EQ(bus.pif[0x7e5], 0xa0U);
    CHECK_EQ(bus.pif[0x7e6], 0U);
    CHECK_EQ(bus.pif[0x7e7], 0U);
    std::fill_n(bus.pif.begin() + 0x7f2, 6, u8{0x55});
    command(system, 0x20);
    for (unsigned i = 0; i < 6; ++i)
        CHECK_EQ(bus.pif[0x7f2 + i], 0x55U);
    command(system, 0x40);
    CHECK(!bus.pif_boot.failed());
}

TEST(pif_boot_timeout_has_a_stable_boundary_under_cpu_and_rcp_advances) {
    for (bool cpu_clock : {false, true})
        for (bool split : {false, true}) {
            System system;
            checksum(system, true);
            auto& bus = system.bus;
            Clock advance{system, cpu_clock};
            const u64 remaining = bus.pif_boot.next_event();
            CHECK(remaining > 374900000 && remaining < 375000000);
            if (split) {
                advance(12345);
                advance(remaining - 12346);
            } else
                advance(remaining - 1);
            CHECK(!bus.pif_boot.failed());
            advance(1);
            CHECK(bus.pif_boot.failed());
            system.cpu.set_pc(0xffffffff80001000ULL);
            system.cpu.step();
            CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
            CHECK_EQ(system.cpu.cp0[30], 0xffffffff80001000ULL);
        }
}

TEST(pif_boot_termination_at_the_deadline_cancels_the_timeout) {
    System system;
    checksum(system, true);
    auto& bus = system.bus;
    bus.tick(bus.pif_boot.next_event() - 1);
    bus.write(0x1fc007fc, 4, 8);
    bus.tick(1);
    CHECK(!bus.pif_boot.failed());
    CHECK_EQ(bus.pif[0x7ff], 0U);
    bus.tick(400000000);
    CHECK(!bus.pif_boot.failed());
}

TEST(pif_boot_failure_repeats_nmi_until_power_reset) {
    System system;
    checksum(system, false);
    auto& bus = system.bus;
    for (unsigned i = 0; i < 3; ++i) {
        system.cpu.set_pc(0xffffffff80001000ULL + 4 * i);
        bus.tick(bus.pif_boot.next_event());
        system.cpu.step();
        CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
        CHECK_EQ(system.cpu.cp0[30], 0xffffffff80001000ULL + 4 * i);
        CHECK(bus.pif_boot.failed());
    }
    command(system, 8);
    CHECK(bus.pif_boot.failed());
    CHECK_EQ(bus.pif[0x7ff], 8U);
    system.reset();
    CHECK(!bus.pif_boot.failed());
    CHECK(!bus.pif_boot.rom_locked());
    checksum(system, true);
    command(system, 8);
    CHECK(!bus.pif_boot.failed());
}

TEST(pif_boot_ignores_boot_commands_after_termination) {
    System system;
    checksum(system, true);
    command(system, 8);
    system.bus.pif[0x7e6] = 0x56;
    system.bus.pif[0x7f2] = 0x78;
    command(system, 0x78);
    CHECK_EQ(system.bus.pif[0x7ff], 0x78U);
    CHECK_EQ(system.bus.pif[0x7e6], 0x56U);
    CHECK_EQ(system.bus.pif[0x7f2], 0x78U);
    CHECK(!system.bus.pif_boot.failed());
}

TEST(pif_boot_accepts_checksum_commands_written_by_si_dma) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    command(system, 0x10);
    for (unsigned i = 0; i < 64; ++i)
        bus.write_ram_byte(0x2000 + i, 0);
    for (unsigned i = 0; i < 6; ++i)
        bus.write_ram_byte(0x2032 + i, static_cast<u8>(bus.cic.checksum() >> ((5 - i) * 8)));
    bus.write_ram_byte(0x203f, 0x20);
    bus.write(0x04800000, 4, 0x2000);
    bus.write(0x04800010, 4, 0x1fc007c0);
    bus.tick(4064);
    CHECK_EQ(bus.pif[0x7ff], 0x10U);
    bus.tick(1);
    CHECK_EQ(bus.pif[0x7ff], 0x20U);
    bus.tick(bus.pif_boot.next_event());
    CHECK_EQ(bus.pif[0x7ff], 0xa0U);
    command(system, 0x40);
    CHECK(!bus.pif_boot.failed());
}

TEST(pif_boot_rejects_a_mismatch_in_each_checksum_bit) {
    for (unsigned bit = 0; bit < 48; ++bit) {
        System system;
        command(system, 0x10);
        const u64 bad = system.bus.cic.checksum() ^ (1ULL << bit);
        for (unsigned i = 0; i < 6; ++i)
            system.bus.pif[0x7f2 + i] = static_cast<u8>(bad >> ((5 - i) * 8));
        command(system, 0x20);
        command(system, 0x48);
        CHECK(system.bus.pif_boot.failed());
        CHECK_EQ(system.bus.pif[0x7ff], 8U);
    }
}

TEST(pif_boot_combined_command_bits_still_follow_each_phase) {
    System system;
    auto& bus = system.bus;
    for (unsigned i = 0; i < 6; ++i)
        bus.pif[0x7f2 + i] = static_cast<u8>(bus.cic.checksum() >> ((5 - i) * 8));
    bus.write(0x1fc007fc, 4, 0x78);
    for (u8 expected : {u8{0x78}, u8{0xf8}, u8{8}, u8{0}}) {
        bus.tick(bus.pif_boot.next_event());
        CHECK_EQ(bus.pif[0x7ff], expected);
        CHECK(!bus.pif_boot.failed());
    }
    CHECK_EQ(bus.pif_boot.next_event(), std::numeric_limits<u64>::max());
}

TEST(pif_boot_poll_phase_survives_idle_clock_advances) {
    for (u64 idle : std::array<u64, 6>{0, 1, 27306, 81920, 1000000000, 4000000000}) {
        System system;
        auto& bus = system.bus;
        bus.tick(idle);
        CHECK(!bus.pif_boot.failed());
        bus.write(0x1fc007fc, 4, 0x10);
        const u64 remaining = (81920 - ((idle % 81920) * 3) % 81920 + 2) / 3;
        CHECK_EQ(bus.pif_boot.next_event(), remaining);
        bus.tick(remaining - 1);
        CHECK(!bus.pif_boot.rom_locked());
        bus.tick(1);
        CHECK(bus.pif_boot.rom_locked());
    }
}

TEST(pif_boot_security_failure_stops_joybus_command_execution) {
    System system;
    test::initialize_memory(system);
    checksum(system, false);
    auto& bus = system.bus;
    ControllerState controller;
    controller.buttons = 0x8000;
    bus.set_controller_state(0, controller);
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c0] = 1;
    bus.pif[0x7c1] = 4;
    bus.pif[0x7c2] = 1;
    bus.pif[0x7c7] = 0xfe;
    bus.joybus.configure();
    command(system, 1);
    CHECK_EQ(bus.pif[0x7ff], 1U);
    bus.write(0x04800000, 4, 0x2000);
    bus.write(0x04800004, 4, 0x1fc007c0);
    bus.tick(37020);
    CHECK_EQ(bus.pif[0x7c3], 0U);
    CHECK_EQ(bus.read_ram_byte(0x2003), 0U);
    CHECK(bus.pif_boot.failed());
}
