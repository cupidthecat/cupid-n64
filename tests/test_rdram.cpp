#include "cupid/rdram.hpp"
#include "test.hpp"

#include <array>
#include <vector>

namespace {

using namespace cupid;

constexpr u32 registers = 0x03f00000;
constexpr u32 broadcast = 0x03f80000;

void initialize(Rdram& memory, unsigned chips = 4) {
    memory.set_bus_active(true);
    memory.write_register(broadcast + 8, 0x00080000, 16);
    for (unsigned chip = 0; chip < chips; ++chip) {
        const unsigned id = chip == 0 ? 8 : chip * 2;
        memory.write_register(registers + 4, id << 26);
        memory.write_register(registers + (id << 10) + 12, 0x02000000);
    }
    memory.write_register(registers + (8U << 10) + 4, 0);
}

struct RamFixture {
    std::vector<u8> bytes;
    Rdram memory;
    explicit RamFixture(unsigned size = 8U * 1024 * 1024) : bytes(size), memory(bytes) {}
};

} // namespace

TEST(rdram_cold_memory_needs_an_active_interface) {
    RamFixture ram;
    ram.memory.write(0, 4, 0x12345678);
    CHECK_EQ(ram.bytes[0], 0U);
    CHECK_EQ(ram.memory.read(0, 4), 0ULL);
    CHECK(ram.memory.acknowledgement_error());
    ram.memory.clear_error();
    CHECK(!ram.memory.acknowledgement_error());
    ram.memory.write_register(broadcast + 8, 0x00080000, 16);
    ram.memory.set_bus_active(true);
    ram.memory.write_register(registers + 12, 0x02000000);
    CHECK_EQ(ram.memory.read_register(registers), 0U);
}

TEST(rdram_initial_write_delay_requires_a_long_repeated_packet) {
    RamFixture ram;
    ram.memory.set_bus_active(true);
    ram.memory.write_register(broadcast + 8, 0x00000008, 0);
    ram.memory.write_register(registers + 12, 0x02000000);
    CHECK_EQ(ram.memory.read_register(registers), 0U);
    ram.memory.write_register(broadcast + 8, 0x00080000, 15);
    ram.memory.write_register(registers + 12, 0x02000000);
    CHECK_EQ(ram.memory.read_register(registers), 0U);
    ram.memory.write_register(broadcast + 8, 0x00080000, 16);
    ram.memory.write_register(registers + 12, 0x02000000);
    CHECK_EQ(ram.memory.read_register(registers), 0xb4190010U);
    CHECK_EQ(ram.memory.read_register(registers + 8), 0x0303020bU);
}

TEST(rdram_device_assignment_exposes_each_physical_chip_once) {
    RamFixture ram;
    initialize(ram.memory);
    for (unsigned chip = 0; chip < 4; ++chip) {
        const u32 address = chip * 0x200000U;
        ram.memory.write(address, 4, 0x12345670U + chip);
        CHECK_EQ(read_be32(ram.bytes.data() + address), 0x12345670U + chip);
        CHECK_EQ(ram.memory.read(address, 4), static_cast<u64>(0x12345670U + chip));
        CHECK_EQ(ram.memory.read_register(registers + chip * 0x800U), 0xb4190010U);
    }
    CHECK_EQ(ram.memory.read_register(broadcast), 0U);
}

TEST(rdram_disabling_interface_blocks_previously_mapped_memory) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0x100, 4, 0x01234567);
    ram.memory.set_bus_active(false);
    CHECK_EQ(ram.memory.read(0x100, 4), 0ULL);
    ram.memory.write(0x100, 4, 0xdeadbeef);
    CHECK_EQ(ram.memory.read_register(registers), 0U);
    ram.memory.set_bus_active(true);
    CHECK_EQ(ram.memory.read(0x100, 4), 0x01234567ULL);
}

TEST(rdram_device_id_moves_memory_without_copying_it) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0x100, 4, 0xabcdef01);
    ram.memory.write_register(registers + 4, 10U << 26);
    CHECK_EQ(ram.memory.read(0x100, 4), 0ULL);
    CHECK_EQ(ram.memory.read(0x00a00100, 4), 0xabcdef01ULL);
    CHECK_EQ(ram.memory.read_register(registers + (10U << 10)), 0xb4190010U);
    ram.memory.write_register(registers + (10U << 10) + 4, 0);
    CHECK_EQ(ram.memory.read(0x100, 4), 0xabcdef01ULL);
}

TEST(rdram_current_calibration_changes_read_reliability) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0x100, 8, ~0ULL);
    ram.memory.write_register(registers + 12, 0x02c0c0c0);
    CHECK_EQ(ram.memory.read(0x100, 8), 0ULL);
    CHECK_EQ(read_be32(ram.bytes.data() + 0x100), 0xffffffffU);
    ram.memory.write_register(registers + 12, 0x02000000);
    CHECK_EQ(ram.memory.read(0x100, 8), ~0ULL);
}

TEST(rdram_mode_readback_inverts_bus_control_bits) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write_register(registers + 12, 0x82004080);
    CHECK_EQ(ram.memory.read_register(registers + 12), 0x42c08040U);
}

TEST(rdram_row_commands_broadcast_and_ignore_register_index_aliases) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write_register(broadcast + 0x200, 0x12345678);
    for (unsigned chip = 0; chip < 4; ++chip) {
        CHECK_EQ(ram.memory.read_register(registers + (chip * 2 << 10) + 0x3fc), 0x12345678U);
    }
    ram.memory.write_register(registers + (2U << 10) + 0x300, 0xabcdef01);
    CHECK_EQ(ram.memory.read_register(registers + 0x200), 0x12345678U);
    CHECK_EQ(ram.memory.read_register(registers + (2U << 10) + 0x200), 0xabcdef01U);
    CHECK_EQ(ram.memory.read_register(registers + 0x40), 0xb4190010U);
    CHECK_EQ(ram.memory.read_register(registers + 0x28), 0U);
}

TEST(rdram_read_only_identification_registers_ignore_writes) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write_register(registers, ~0U);
    ram.memory.write_register(registers + 36, ~0U);
    CHECK_EQ(ram.memory.read_register(registers), 0xb4190010U);
    CHECK_EQ(ram.memory.read_register(registers + 36), 0x500U);
}

TEST(rdram_absent_expansion_memory_does_not_mirror_base_memory) {
    RamFixture ram(4U * 1024 * 1024);
    initialize(ram.memory, 2);
    ram.memory.write(0, 8, 0x0123456789abcdefULL);
    ram.memory.write(0x400000, 8, ~0ULL);
    CHECK_EQ(ram.memory.read(0, 8), 0x0123456789abcdefULL);
    CHECK_EQ(ram.memory.read(0x400000, 8), 0ULL);
    CHECK_EQ(ram.memory.read_register(registers + (4U << 10)), 0U);
}

TEST(rdram_regular_stores_update_the_ninth_bit_by_transfer_width) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0, 4, 0x00010001);
    CHECK_EQ(ram.memory.read(0, 4, true), 15ULL);
    ram.memory.write(0, 1, 0xff);
    CHECK_EQ(ram.memory.read(0, 4, true), 7ULL);
    ram.memory.write(1, 1, 0xfe);
    CHECK_EQ(ram.memory.read(0, 4, true), 3ULL);
    ram.memory.write(0, 2, 1);
    CHECK_EQ(ram.memory.read(0, 4, true), 15ULL);
    ram.memory.write(2, 2, 0x1000);
    CHECK_EQ(ram.memory.read(0, 4, true), 12ULL);
    ram.memory.write(0, 8, 0x0001000000000001ULL);
    CHECK_EQ(ram.memory.read(0, 8, true), 0x0000000c00000003ULL);
}

TEST(rdram_ebus_reads_expose_only_the_low_nibble_of_each_word) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0, 4, 0x00010001);
    CHECK_EQ(ram.memory.read(0, 1, true), 0ULL);
    CHECK_EQ(ram.memory.read(1, 1, true), 0ULL);
    CHECK_EQ(ram.memory.read(2, 1, true), 0ULL);
    CHECK_EQ(ram.memory.read(3, 1, true), 15ULL);
    CHECK_EQ(ram.memory.read(0, 2, true), 0ULL);
    CHECK_EQ(ram.memory.read(2, 2, true), 15ULL);
}

TEST(rdram_ebus_writes_scatter_bits_and_preserve_ordinary_data) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0, 8, 0x89abcdea01234567ULL, true);
    CHECK_EQ(ram.memory.read(0, 8), 0x89abcdea01234567ULL);
    CHECK_EQ(ram.memory.read(0, 8, true), 0x0000000a00000007ULL);
    ram.memory.write(0, 1, 0xff, true);
    CHECK_EQ(ram.memory.read(0, 4, true), 2ULL);
    ram.memory.write(1, 1, 0xff, true);
    CHECK_EQ(ram.memory.read(0, 4, true), 2ULL);
    ram.memory.write(2, 2, 1, true);
    CHECK_EQ(ram.memory.read(0, 4, true), 1ULL);
    ram.memory.write(0, 2, 0xffff, true);
    CHECK_EQ(ram.memory.read(0, 4, true), 1ULL);
}

TEST(rdram_pixel_coverage_bits_can_be_written_independently) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0x100, 2, 0xf801);
    ram.memory.set_hidden_pair(0x100, 2);
    CHECK_EQ(ram.memory.hidden_pair(0x100), 2U);
    CHECK_EQ(ram.memory.hidden_pair(0x101), 2U);
    CHECK_EQ(ram.memory.read(0x100, 2), 0xf801ULL);
    CHECK_EQ(ram.memory.read(0x100, 4, true), 8ULL);
}

TEST(rdram_warm_reset_keeps_cells_and_device_configuration) {
    RamFixture ram;
    initialize(ram.memory);
    ram.memory.write(0x100, 8, 0x123456789abcdef0ULL);
    ram.memory.reset(true);
    ram.memory.set_bus_active(true);
    CHECK_EQ(ram.memory.read(0x100, 8), 0x123456789abcdef0ULL);
    CHECK_EQ(ram.memory.read_register(registers), 0xb4190010U);
}
