#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>

namespace {
using namespace cupid;
constexpr u32 Error = 0x04700018;
constexpr u32 BankStatus = 0x0470001c;
} // namespace

TEST(ri_bank_status_write_clears_valid_and_sets_dirty_bits) {
    System system;
    test::initialize_memory(system);
    for (u32 value : {0U, 0xffffffffU, 0x12345678U}) {
        system.bus.write(BankStatus, 4, value);
        CHECK_EQ(system.bus.read(BankStatus, 4), 0xff00U);
    }
}

TEST(ri_reads_open_independent_rows_in_each_one_megabyte_bank) {
    System system;
    test::initialize_memory(system);
    for (u32 bank = 0; bank < 8; ++bank) {
        CHECK_EQ(system.bus.read(bank * 0x100000 + 0x2000, 4), 0U);
        CHECK_EQ(system.bus.read(BankStatus, 4), (1U << (bank + 1)) - 1);
    }
}

TEST(ri_dirty_state_survives_reads_until_a_different_row_opens) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.write(0x100800, 1, 0x12);
    bus.write(0x700, 8, 0xabcdef);
    CHECK_EQ(bus.read(BankStatus, 4), 0x0303U);
    CHECK_EQ(bus.read(0x100fff, 1), 0U);
    CHECK_EQ(bus.read(BankStatus, 4), 0x0303U);
    CHECK_EQ(bus.read(0x101000, 2), 0U);
    CHECK_EQ(bus.read(BankStatus, 4), 0x0103U);
    CHECK_EQ(bus.read(0x100800, 1), 0x12U);
    CHECK_EQ(bus.read(BankStatus, 4), 0x0103U);
}

TEST(ri_cache_and_dma_memory_paths_share_row_tracking) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    std::array<u8, 16> line{};
    CHECK(bus.write_cache(0x200800, line));
    CHECK_EQ(bus.read(BankStatus, 4), 0x0404U);
    CHECK(bus.read_cache(0x201000, line));
    CHECK_EQ(bus.read(BankStatus, 4), 0x0004U);
    bus.write_ram_byte(0x300000, 0x56);
    CHECK_EQ(bus.read(BankStatus, 4), 0x080cU);
    CHECK_EQ(bus.read_ram_byte(0x300800), 0U);
    CHECK_EQ(bus.read(BankStatus, 4), 0x000cU);
}

TEST(ri_absent_expansion_memory_sets_ack_error_on_the_identity_path) {
    System system;
    system.bus.rdram.resize(0x400000);
    test::initialize_memory(system);
    auto& bus = system.bus;
    CHECK_EQ(bus.read(0x400000, 4), 0U);
    CHECK_EQ(bus.read(Error, 4), 1U);
    CHECK_EQ(bus.read(0x04700008, 4) & 1U, 1U);
    bus.write(Error, 4, 0);
    CHECK_EQ(bus.read(Error, 4), 0U);
    bus.write(0x400000, 4, 0x12345678);
    CHECK_EQ(bus.read(Error, 4), 1U);
}

TEST(ri_out_of_range_error_is_sticky_and_does_not_alias_a_bank) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.write(0x100, 4, 0x12345678);
    CHECK_EQ(bus.read(0x800100, 4), 0U);
    CHECK_EQ(bus.read(Error, 4), 5U);
    CHECK_EQ(bus.read(BankStatus, 4), 0x0101U);
    CHECK_EQ(bus.read(0x100, 4), 0x12345678U);
    CHECK_EQ(bus.read(Error, 4), 5U);
    bus.write(Error, 4, 0xffffffff);
    CHECK_EQ(bus.read(Error, 4), 0U);
    bus.write(0x03effffc, 4, 0x87654321);
    CHECK_EQ(bus.read(Error, 4), 5U);
    bus.write(Error, 4, 0);
    CHECK_EQ(bus.read(0x03f00000, 4), 0xb4190010U);
    CHECK_EQ(bus.read(Error, 4), 0U);
}

TEST(ri_mapped_memory_above_eight_megabytes_still_sets_range_error) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.write(0x03f00004, 4, 10U << 26);
    bus.write(0x00a00100, 4, 0x12345678);
    CHECK_EQ(bus.read(Error, 4), 4U);
    CHECK_EQ(bus.read(BankStatus, 4), 0U);
    bus.write(Error, 4, 0);
    CHECK_EQ(bus.read(0x00a00100, 4), 0x12345678U);
    CHECK_EQ(bus.read(Error, 4), 4U);
    CHECK_EQ(bus.read(BankStatus, 4), 0U);
}

TEST(ri_reset_clears_bank_tracking_and_error_latches) {
    System system;
    test::initialize_memory(system);
    system.bus.write(0x700000, 2, 0xffff);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x8080U);
    CHECK_EQ(system.bus.read(0x800000, 4), 0U);
    CHECK_EQ(system.bus.read(Error, 4), 5U);
    system.reset();
    CHECK_EQ(system.bus.read(BankStatus, 4), 0U);
    CHECK_EQ(system.bus.read(Error, 4), 0U);
}

TEST(ri_cache_hits_do_not_reopen_rows_or_write_back_dirty_cache_data) {
    System system;
    test::initialize_memory(system);
    system.cpu.write_cop0(12, 0x34000000);
    u64 value = 0;
    CHECK(system.cpu.read_memory(0xffffffff80001000ULL, 4, value));
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x0001U);
    CHECK(system.cpu.write_memory(0xffffffff80001000ULL, 4, 0x12345678));
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x0001U);
    system.bus.write(0x1800, 4, 0xabcdef01);
    CHECK(system.cpu.read_memory(0xffffffff80001000ULL, 4, value));
    CHECK_EQ(value, 0x12345678U);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x0101U);
    system.cpu.cache_operation(0x19, 0xffffffff80001000ULL);
    CHECK_EQ(system.bus.read(0x1000, 4), 0x12345678U);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x0101U);
    CHECK_EQ(system.bus.read(0x1800, 4), 0xabcdef01U);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x0001U);
}

TEST(ri_bank_tracking_uses_the_bus_address_not_the_chip_storage_offset) {
    System system;
    test::initialize_memory(system);
    system.bus.write(0x03f00004, 4, 6U << 26);
    system.bus.write(0x600000, 4, 0x12345678);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x4040U);
    CHECK_EQ(system.bus.read(0x600800, 4), 0U);
    CHECK_EQ(system.bus.read(BankStatus, 4), 0x0040U);
}
