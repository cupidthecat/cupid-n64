#include "cupid/rdram.hpp"
#include "test.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;

constexpr u32 registers = 0x03f00000U;
constexpr u32 broadcast = 0x03f80000U;

struct Fixture {
    std::vector<u8> bytes;
    Rdram memory;

    explicit Fixture(unsigned size = 8U * 1024U * 1024U) : bytes(size), memory(bytes) {}

    void initialize(unsigned chips = 4) {
        memory.set_bus_active(true);
        memory.write_register(broadcast + 8, 0x00080000U, 16);
        for (unsigned chip = 0; chip < chips; ++chip) {
            const unsigned id = chip == 0 ? 8U : chip * 2U;
            memory.write_register(registers + 4, id << 26U);
            memory.write_register(registers + (id << 10U) + 12U, 0x02000000U);
        }
        memory.write_register(registers + (8U << 10U) + 4U, 0);
    }
};

struct Transfer {
    unsigned width;
    u64 value;
};

constexpr std::array<Transfer, 4> transfers{{
    {1, 0xa5U},
    {2, 0x1234U},
    {4, 0x89abcdefU},
    {8, 0x0123456789abcdefULL},
}};

u8 transfer_byte(u64 value, unsigned width, unsigned index) {
    return static_cast<u8>(value >> ((width - index - 1U) * 8U));
}

} // namespace

TEST(rdram_packed_transfers_preserve_big_endian_bytes_and_align_down) {
    Fixture fixture;
    fixture.initialize();

    for (std::size_t index = 0; index < transfers.size(); ++index) {
        const auto [width, value] = transfers[index];
        const u32 base = 0x100U + static_cast<u32>(index) * 0x20U;
        for (u32 address = base - 1U; address <= base + width; ++address)
            fixture.bytes[address] = 0xccU;

        fixture.memory.write(base + width - 1U, width, value);
        CHECK_EQ(fixture.bytes[base - 1U], 0xccU);
        CHECK_EQ(fixture.bytes[base + width], 0xccU);
        for (unsigned byte = 0; byte < width; ++byte)
            CHECK_EQ(fixture.bytes[base + byte], transfer_byte(value, width, byte));
        CHECK_EQ(fixture.memory.read(base + width - 1U, width), value);
    }
}

TEST(rdram_packed_regular_writes_keep_ninth_bit_rules_for_every_width) {
    Fixture fixture;
    fixture.initialize();

    constexpr u32 byte_base = 0x200U;
    fixture.memory.set_hidden_pair(byte_base, 3);
    fixture.memory.write(byte_base, 1, 0xffU);
    CHECK_EQ(fixture.memory.hidden_pair(byte_base), 1U);
    fixture.memory.write(byte_base + 1U, 1, 0xffU);
    CHECK_EQ(fixture.memory.hidden_pair(byte_base), 1U);
    fixture.memory.write(byte_base + 1U, 1, 0xfeU);
    CHECK_EQ(fixture.memory.hidden_pair(byte_base), 0U);

    constexpr u32 half_base = 0x220U;
    fixture.memory.set_hidden_pair(half_base, 2);
    fixture.memory.write(half_base + 1U, 2, 0x0001U);
    CHECK_EQ(fixture.memory.hidden_pair(half_base), 3U);

    constexpr u32 word_base = 0x240U;
    fixture.memory.set_hidden_pair(word_base, 2);
    fixture.memory.set_hidden_pair(word_base + 2U, 2);
    fixture.memory.write(word_base + 3U, 4, 0x00010000U);
    CHECK_EQ(fixture.memory.hidden_pair(word_base), 3U);
    CHECK_EQ(fixture.memory.hidden_pair(word_base + 2U), 0U);

    constexpr u32 double_base = 0x260U;
    for (unsigned pair = 0; pair < 4; ++pair)
        fixture.memory.set_hidden_pair(double_base + pair * 2U, 2);
    fixture.memory.write(double_base + 7U, 8, 0x0001000000010001ULL);
    CHECK_EQ(fixture.memory.hidden_pair(double_base), 3U);
    CHECK_EQ(fixture.memory.hidden_pair(double_base + 2U), 0U);
    CHECK_EQ(fixture.memory.hidden_pair(double_base + 4U), 3U);
    CHECK_EQ(fixture.memory.hidden_pair(double_base + 6U), 3U);
}

TEST(rdram_packed_transfers_follow_device_remapping_without_copying_backing_bytes) {
    Fixture fixture;
    fixture.initialize();
    fixture.memory.write(0x100U, 8, 0x0123456789abcdefULL);

    fixture.memory.write_register(registers + 4U, 10U << 26U);
    CHECK_EQ(fixture.memory.read(0x100U, 8), 0U);
    CHECK(fixture.memory.acknowledgement_error());
    fixture.memory.clear_error();

    CHECK_EQ(fixture.memory.read(0x00a00107U, 8), 0x0123456789abcdefULL);
    CHECK_EQ(fixture.memory.errors(), 4U);
    fixture.memory.clear_error();

    fixture.memory.write(0x00a00103U, 4, 0xa1b2c3d4U);
    CHECK_EQ(fixture.memory.errors(), 4U);
    CHECK_EQ(read_be32(fixture.bytes.data() + 0x100U), 0xa1b2c3d4U);
    CHECK_EQ(read_be32(fixture.bytes.data() + 0x104U), 0x89abcdefU);
}

TEST(rdram_packed_reads_apply_low_current_reliability_to_byte_half_and_word_widths) {
    Fixture fixture;
    fixture.initialize();
    fixture.memory.write(0x400U, 8, 0xffffffffffffffffULL);

    fixture.memory.write_register(registers + 12U, 0x02c0c0c0U);
    CHECK_EQ(fixture.memory.read(0x400U, 1), 0U);
    CHECK_EQ(fixture.memory.read(0x402U, 2), 0U);
    CHECK_EQ(fixture.memory.read(0x404U, 4), 0U);

    fixture.memory.write_register(registers + 12U, 0x02000000U);
    CHECK_EQ(fixture.memory.read(0x400U, 1), 0xffU);
    CHECK_EQ(fixture.memory.read(0x402U, 2), 0xffffU);
    CHECK_EQ(fixture.memory.read(0x404U, 4), 0xffffffffU);
}

TEST(rdram_packed_unmapped_and_inactive_accesses_keep_tracking_and_error_order) {
    {
        Fixture fixture;
        fixture.bytes[0x100U] = 0x5aU;
        CHECK_EQ(fixture.memory.read(0x103U, 4), 0U);
        CHECK(fixture.memory.acknowledgement_error());
        CHECK_EQ(fixture.memory.bank_status(), 0U);
        fixture.memory.write(0x103U, 4, 0x12345678U);
        CHECK_EQ(fixture.bytes[0x100U], 0x5aU);
        CHECK_EQ(fixture.memory.read(0x103U, 4, true), 0U);
    }

    {
        Fixture fixture;
        fixture.initialize();
        fixture.memory.write_register(registers + 4U, 10U << 26U);
        fixture.memory.clear_error();
        CHECK_EQ(fixture.memory.read(0x103U, 4), 0U);
        CHECK_EQ(fixture.memory.errors(), 1U);
        CHECK_EQ(fixture.memory.bank_status() & 1U, 1U);
        CHECK_EQ(fixture.bytes[0x100U], 0U);
    }
}

TEST(rdram_packed_data_path_preserves_ebus_fallback_for_every_width) {
    Fixture fixture;
    fixture.initialize();

    fixture.memory.write(0x303U, 1, 0xffU, true);
    CHECK_EQ(fixture.bytes[0x303U], 0xffU);
    CHECK_EQ(fixture.memory.read(0x303U, 1), 0xffU);
    CHECK_EQ(fixture.memory.read(0x303U, 1, true), 1U);

    fixture.memory.write(0x322U, 2, 0x0003U, true);
    CHECK_EQ(read_be16(fixture.bytes.data() + 0x322U), 0x0003U);
    CHECK_EQ(fixture.memory.read(0x323U, 2, true), 3U);

    fixture.memory.write(0x340U, 4, 0x89abcdefU, true);
    CHECK_EQ(read_be32(fixture.bytes.data() + 0x340U), 0x89abcdefU);
    CHECK_EQ(fixture.memory.read(0x343U, 4, true), 0x0fU);

    fixture.memory.write(0x360U, 8, 0x1234567a89abcde5ULL, true);
    CHECK_EQ(read_be64(fixture.bytes.data() + 0x360U), 0x1234567a89abcde5ULL);
    CHECK_EQ(fixture.memory.read(0x367U, 8, true), 0x0000000a00000005ULL);
}
