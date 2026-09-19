#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {

using namespace cupid;

std::unique_ptr<System> memory_machine() {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    return system;
}

void repeat(Bus& bus, unsigned bytes) {
    bus.write(0x04300000, 4, 0x100U | (bytes - 1));
}

} // namespace

TEST(memory_bus_current_load_and_select_enable_the_interface_together) {
    auto system = std::make_unique<System>();
    auto& bus = system->bus;
    CHECK(!bus.memory.bus_active());
    bus.write(0x0470000c, 4, 0x14);
    CHECK(!bus.memory.bus_active());
    bus.write(0x04700008, 4, 0);
    CHECK(bus.memory.bus_active());
    bus.write(0x0470000c, 4, 0x10);
    CHECK(!bus.memory.bus_active());
    bus.write(0x0470000c, 4, 0x14);
    CHECK(bus.memory.bus_active());
}

TEST(memory_bus_cold_chip_setup_uses_repeated_register_writes) {
    auto system = std::make_unique<System>();
    auto& bus = system->bus;
    bus.write(0x04700008, 4, 0);
    bus.write(0x0470000c, 4, 0x14);
    repeat(bus, 16);
    bus.write(0x03f80008, 4, 0x00080000);
    CHECK_EQ(bus.read(0x04300000, 4) & 0x80U, 0ULL);
    bus.write(0x03f0000c, 4, 0x02000000);
    CHECK_EQ(bus.read(0x03f00000, 4), 0xb4190010ULL);
    CHECK_EQ(bus.read(0x03f00008, 4), 0x0303020bULL);
    bus.write(0x00000100, 8, 0x0123456789abcdefULL);
    CHECK_EQ(bus.read(0x00000100, 8), 0x0123456789abcdefULL);
}

TEST(memory_bus_register_select_controls_second_word_reads) {
    auto system = memory_machine();
    auto& bus = system->bus;
    bus.write(0x03f00004, 4, 1U << 26);
    CHECK_EQ(bus.read(0x03f00004, 4), 0ULL);
    CHECK_EQ(bus.read(0x03f00000, 8), 0xb419001004000000ULL);
    bus.write(0x04300000, 4, 0x2000);
    CHECK_EQ(bus.read(0x03f00004, 4), 0x04000000ULL);
    CHECK_EQ(bus.read(0x03f00000, 1), 0xb4ULL);
    CHECK_EQ(bus.read(0x03f00002, 2), 0x0010ULL);
    bus.write(0x04300000, 4, 0x1000);
    CHECK_EQ(bus.read(0x03f00004, 4), 0ULL);
}

TEST(memory_bus_register_doubleword_store_only_uses_its_high_word) {
    auto system = memory_machine();
    auto& bus = system->bus;
    bus.write(0x04300000, 4, 0x2000);
    bus.write(0x03f00010, 8, 0x1234567889abcdefULL);
    CHECK_EQ(bus.read(0x03f00010, 4), 0x12345678ULL);
    CHECK_EQ(bus.read(0x03f00014, 4), 0ULL);
}

TEST(memory_bus_repeat_is_consumed_once_and_keeps_its_length) {
    auto system = memory_machine();
    auto& bus = system->bus;
    repeat(bus, 24);
    bus.write(0x100, 4, 0x89abcdef);
    for (u32 offset = 0; offset < 24; offset += 4)
        CHECK_EQ(bus.read(0x100 + offset, 4), 0x89abcdefULL);
    CHECK_EQ(bus.read(0x118, 4), 0ULL);
    CHECK_EQ(bus.read(0x04300000, 4) & 0xffU, 23ULL);
    bus.write(0x200, 4, 0x01234567);
    CHECK_EQ(bus.read(0x200, 4), 0x01234567ULL);
    CHECK_EQ(bus.read(0x204, 4), 0ULL);
}

TEST(memory_bus_repeat_preserves_doubleword_phase_at_unaligned_start) {
    auto system = memory_machine();
    auto& bus = system->bus;
    repeat(bus, 16);
    bus.write(0x104, 4, 0x11223344);
    CHECK_EQ(bus.read(0x100, 8), 0x0000000011223344ULL);
    CHECK_EQ(bus.read(0x108, 8), 0x1122334411223344ULL);
    CHECK_EQ(bus.read(0x110, 8), 0ULL);
    repeat(bus, 16);
    bus.write(0x202, 2, 0x89abcdef);
    CHECK_EQ(bus.read(0x200, 8), 0x0000cdef89abcdefULL);
    CHECK_EQ(bus.read(0x208, 8), 0x89abcdef89abcdefULL);
}

TEST(memory_bus_repeat_byte_lane_uses_the_full_bus_word) {
    auto system = memory_machine();
    auto& bus = system->bus;
    repeat(bus, 16);
    bus.write(0x103, 1, 0x12345678);
    CHECK_EQ(bus.read(0x100, 8), 0x0000007812345678ULL);
    CHECK_EQ(bus.read(0x108, 8), 0x1234567812345678ULL);
    repeat(bus, 8);
    bus.write(0x200, 1, 0x12345678);
    CHECK_EQ(bus.read(0x200, 8), 0x7800000078000000ULL);
}

TEST(memory_bus_repeat_wraps_inside_the_initial_two_kibibyte_row) {
    auto system = memory_machine();
    auto& bus = system->bus;
    repeat(bus, 24);
    bus.write(0x7f8, 8, 0x0123456789abcdefULL);
    CHECK_EQ(bus.read(0x7f8, 8), 0x0123456789abcdefULL);
    CHECK_EQ(bus.read(0, 8), 0x0123456789abcdefULL);
    CHECK_EQ(bus.read(8, 8), 0x0123456789abcdefULL);
    CHECK_EQ(bus.read(0x800, 8), 0ULL);
}

TEST(memory_bus_repeat_does_not_extend_past_a_short_packet) {
    auto system = memory_machine();
    auto& bus = system->bus;
    repeat(bus, 1);
    bus.write(0x107, 1, 0x55);
    CHECK_EQ(bus.read(0x100, 8), 0ULL);
    CHECK_EQ(bus.read(0x04300000, 4) & 0x80U, 0ULL);
    repeat(bus, 3);
    bus.write(0x200, 4, 0x11223344);
    CHECK_EQ(bus.read(0x200, 8), 0x1122330000000000ULL);
}

TEST(memory_bus_repeat_updates_hidden_bits_using_real_transfer_widths) {
    auto system = memory_machine();
    auto& bus = system->bus;
    repeat(bus, 16);
    bus.write(0x100, 4, 0x00010001);
    for (u32 offset = 0; offset < 16; offset += 2)
        CHECK_EQ(bus.memory.hidden_pair(0x100 + offset), 3U);
    repeat(bus, 8);
    bus.write(0x202, 2, 0x00010001);
    CHECK_EQ(bus.memory.hidden_pair(0x200), 0U);
    CHECK_EQ(bus.memory.hidden_pair(0x202), 3U);
    CHECK_EQ(bus.memory.hidden_pair(0x204), 3U);
    CHECK_EQ(bus.memory.hidden_pair(0x206), 3U);
}

TEST(memory_bus_ebus_mode_changes_uncached_accesses_without_affecting_dma) {
    auto system = memory_machine();
    auto& bus = system->bus;
    bus.write(0x100, 4, 0x12350001);
    bus.write(0x04300000, 4, 0x400);
    CHECK_EQ(bus.read(0x100, 4), 15ULL);
    CHECK_EQ(bus.read_ram_byte(0x100), 0x12U);
    bus.write(0x100, 4, 0xabcdef05);
    CHECK_EQ(bus.read(0x100, 4), 5ULL);
    CHECK_EQ(bus.memory.read(0x100, 4), 0xabcdef05ULL);
    bus.write(0x04300000, 4, 0x200);
    CHECK_EQ(bus.read(0x100, 4), 0xabcdef05ULL);
}

TEST(memory_bus_repeat_takes_precedence_over_ebus_mode) {
    auto system = memory_machine();
    auto& bus = system->bus;
    bus.write(0x04300000, 4, 0x400);
    repeat(bus, 8);
    bus.write(0x100, 4, 0x00010001);
    CHECK_EQ(bus.read(0x100, 4), 15ULL);
    CHECK_EQ(bus.read(0x104, 4), 15ULL);
    bus.write(0x108, 4, 0x00010001);
    CHECK_EQ(bus.read(0x108, 4), 1ULL);
}

TEST(memory_bus_cached_write_does_not_consume_repeat_mode) {
    auto system = memory_machine();
    auto& bus = system->bus;
    std::array<u8, 16> line{};
    write_be32(line.data(), 0x89abcdef);
    repeat(bus, 32);
    CHECK(bus.write_cache(0x100, line));
    CHECK_EQ(bus.read(0x100, 4), 0x89abcdefULL);
    CHECK_EQ(bus.read(0x110, 4), 0ULL);
    CHECK_EQ(bus.read(0x04300000, 4) & 0x80U, 0x80ULL);
    bus.write(0x200, 4, 0x12345678);
    CHECK_EQ(bus.read(0x21c, 4), 0x12345678ULL);
    CHECK_EQ(bus.read(0x04300000, 4) & 0x80U, 0ULL);
}

TEST(memory_bus_cached_register_read_returns_one_word) {
    auto system = memory_machine();
    auto& bus = system->bus;
    std::array<u8, 32> line;
    line.fill(0xff);
    CHECK(bus.read_cache(0x03f00000, line));
    CHECK_EQ(read_be32(line.data()), 0xb4190010U);
    for (std::size_t byte = 4; byte < line.size(); ++byte)
        CHECK_EQ(line[byte], 0U);
}

TEST(memory_bus_cached_access_in_ebus_mode_stalls_the_cpu) {
    auto system = memory_machine();
    auto& bus = system->bus;
    bus.write(0x04300000, 4, 0x400);
    std::array<u8, 16> line{};
    CHECK(!bus.read_cache(0x100, line));
    CHECK(system->cpu.frozen);
    system->cpu.frozen = false;
    CHECK(!bus.write_cache(0x100, line));
    CHECK(system->cpu.frozen);
}

TEST(memory_bus_dma_out_of_range_accesses_do_not_alias_memory) {
    auto system = memory_machine();
    auto& bus = system->bus;
    bus.write_ram_byte(0, 0x12);
    bus.write_ram_byte(0x800000, 0xff);
    CHECK_EQ(bus.read_ram_byte(0), 0x12U);
    CHECK_EQ(bus.read_ram_byte(0x800000), 0U);
    CHECK_EQ(bus.read(0x01000000, 4), 0ULL);
}

TEST(memory_bus_acknowledgement_error_is_visible_and_clearable) {
    auto system = std::make_unique<System>();
    auto& bus = system->bus;
    CHECK_EQ(bus.read(0x04700018, 4), 0ULL);
    CHECK_EQ(bus.read(0x100, 4), 0ULL);
    CHECK_EQ(bus.read(0x04700018, 4) & 1U, 1ULL);
    CHECK_EQ(bus.read(0x04700008, 4) & 7U, 7ULL);
    bus.write(0x04700018, 4, ~0U);
    CHECK_EQ(bus.read(0x04700018, 4), 0ULL);
    CHECK_EQ(bus.read(0x04700008, 4) & 7U, 6ULL);
    bus.write(0x0470001c, 4, 0);
    CHECK_EQ(bus.read(0x0470001c, 4), 0xffULL);
}
