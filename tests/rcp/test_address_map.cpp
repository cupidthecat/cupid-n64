#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;
}

TEST(bus_pif_address_window_mirrors_the_two_kibibyte_image) {
    System system;
    system.bus.pif[0] = 0x12;
    system.bus.pif[1] = 0x34;
    system.bus.pif[2] = 0x56;
    system.bus.pif[3] = 0x78;
    for (u32 base : {0x1fc00000U, 0x1fc00800U, 0x1fc80000U, 0x1fcff800U}) {
        CHECK_EQ(system.bus.read(base, 4), 0x12345678U);
        CHECK_EQ(system.bus.read(base + 2, 2), 0x5678U);
        system.bus.write(base + 0x7c0, 4, base);
        CHECK_EQ(system.bus.read(0x1fc007c0, 4), base);
    }
    CHECK(!system.cpu.frozen);
}

TEST(bus_unmapped_rcp_accesses_stall_instead_of_returning_old_data) {
    for (u32 address : {0x040c0000U, 0x040ffffcU, 0x04900000U, 0x04fffffcU, 0x80000000U}) {
        for (bool write : {false, true}) {
            System system;
            CHECK_EQ(system.bus.read(0x04300004, 4), 0x02020102U);
            if (write)
                system.bus.write(address, 4, 0xdeadbeefU);
            else
                CHECK_EQ(system.bus.read(address, 4), 0U);
            CHECK(system.cpu.frozen);
            CHECK(!system.cpu.exception_pending);
        }
    }
}

TEST(cpu_unmapped_load_does_not_commit_its_destination) {
    System system;
    test::initialize_memory(system);
    system.bus.write(0x1000, 4, 0x8c220000); // LW r2, 0(r1).
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.gpr[1] = 0xffffffffa4900000ULL;
    system.cpu.gpr[2] = 0x123456789abcdef0ULL;
    system.cpu.step();
    CHECK(system.cpu.frozen);
    CHECK(!system.cpu.exception_pending);
    CHECK_EQ(system.cpu.gpr[2], 0x123456789abcdef0ULL);
    CHECK_EQ(system.cpu.pc, 0xffffffff80001000ULL);
}

TEST(bus_pi_upper_address_window_reaches_the_cartridge_bus) {
    System system;
    system.bus.write(0x1fd00120U, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x04600010, 4) & 2U, 2U);
    CHECK_EQ(system.bus.read(0x04600004, 4), 0x1fd00124U);
    CHECK_EQ(system.bus.read(0x1fd00120U, 4), 0x12345678U);
    CHECK_EQ(system.bus.read(0x04600010, 4) & 2U, 0U);
    CHECK(!system.cpu.frozen);
}

TEST(bus_pif_rom_lockout_hides_boot_code_until_reset) {
    System system;
    system.bus.pif[0] = 0xab;
    CHECK_EQ(system.bus.read(0x1fc00000, 1), 0xabU);
    system.bus.write(0x1fc007fc, 4, 0x10);
    system.bus.tick(2150);
    CHECK_EQ(system.bus.read(0x1fc00000, 1), 0U);
    CHECK_EQ(system.bus.read(0x1fc00800, 4), 0U);
    system.bus.write(0x1fc007c0, 4, 0x12345678);
    CHECK_EQ(system.bus.read(0x1fc007c0, 4), 0x12345678U);
    system.reset();
    CHECK_EQ(system.bus.read(0x1fc00000, 1), 0xabU);
}
