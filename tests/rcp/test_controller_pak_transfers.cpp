#include "controller_pak_fixture.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;
using test::PakFixture;

// Polynomial long division keeps the oracle independent of the serial CRC loop.
u8 address_remainder(u16 address) {
    unsigned remainder = address;
    for (int bit = 15; bit >= 5; --bit)
        if (remainder & (1U << bit))
            remainder ^= 0x35U << (bit - 5);
    return static_cast<u8>(remainder);
}

u8 data_remainder(std::span<const u8, 32> data) {
    unsigned remainder = 0;
    for (unsigned byte = 0; byte <= data.size(); ++byte) {
        remainder = (remainder << 8) | (byte == data.size() ? 0 : data[byte]);
        for (int bit = 15; bit >= 8; --bit)
            if (remainder & (1U << bit))
                remainder ^= 0x185U << (bit - 8);
    }
    return static_cast<u8>(remainder);
}

std::array<u8, 35> write_packet(u16 address, const std::array<u8, 32>& data) {
    const auto encoded = static_cast<u16>(address | address_remainder(address));
    std::array<u8, 35> input{3, static_cast<u8>(encoded >> 8), static_cast<u8>(encoded)};
    std::copy(data.begin(), data.end(), input.begin() + 3);
    return input;
}

std::vector<u8> request(PakFixture& fixture, unsigned port, std::span<const u8> input, u8 receive) {
    fixture.packet(port, input, receive);
    const auto reply = fixture.execute(receive);
    CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
    CHECK_EQ(fixture.bus.pif[fixture.response + receive], 0xfeU);
    return reply;
}

void read_block(PakFixture& fixture, unsigned port, const std::array<u8, 35>& write,
                const std::array<u8, 32>& expected, bool rejected = false) {
    const std::array<u8, 3> input{2, write[1], write[2]};
    const auto reply = request(fixture, port, input, 33);
    CHECK(std::equal(expected.begin(), expected.end(), reply.begin()));
    CHECK_EQ(reply[32], data_remainder(expected) ^ (rejected ? 0xffU : 0U));
}
} // namespace

TEST(controller_pak_all_block_addresses_and_each_address_crc_bit_on_all_ports) {
    PakFixture fixture;
    fixture.ready();
    auto expected_storage = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned block = 0; block < 2048; ++block) {
            const auto address = static_cast<u16>(block * 32);
            std::array<u8, 32> data{};
            for (unsigned index = 0; index < data.size(); ++index)
                data[index] = static_cast<u8>(block + (block >> 8) * 43 + index * 17 + port * 29);
            const auto write = write_packet(address, data);
            const auto crc = data_remainder(data);
            CHECK_EQ(request(fixture, port, write, 1)[0], crc);
            if (address < 0x8000)
                std::copy(data.begin(), data.end(), expected_storage[port].begin() + address);
            CHECK(fixture.bus.controller_paks == expected_storage);
            read_block(fixture, port, write, address < 0x8000 ? data : std::array<u8, 32>{});
            for (unsigned bit = 0; bit < 5; ++bit) {
                auto corrupt = write;
                corrupt[2] ^= static_cast<u8>(1U << bit);
                CHECK_EQ(request(fixture, port, corrupt, 1)[0], crc ^ 0xffU);
                CHECK(fixture.bus.controller_paks == expected_storage);
                read_block(fixture, port, corrupt, {}, true);
            }
        }
    fixture.system.reset();
    CHECK(fixture.bus.controller_paks == expected_storage);
}

TEST(controller_pak_data_crc_known_vectors_one_hot_bits_and_mixed_blocks) {
    PakFixture fixture;
    fixture.ready();
    std::array<u8, 32> data{};
    CHECK_EQ(data_remainder(data), 0U);
    data.fill(0x12);
    CHECK_EQ(data_remainder(data), 0x44U);
    data.fill(0xff);
    CHECK_EQ(data_remainder(data), 0x0aU);
    u32 random = 0x12345678;
    for (unsigned pattern = 0; pattern < 515; ++pattern) {
        data.fill(0);
        if (pattern < 256) {
            data[pattern / 8] = static_cast<u8>(1U << (pattern % 8));
        } else if (pattern < 512) {
            for (auto& byte : data) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                byte = static_cast<u8>(random);
            }
        } else {
            data.fill(pattern == 512 ? 0 : pattern == 513 ? 0xff : 0x12);
        }
        for (unsigned port = 0; port < 4; ++port) {
            const auto write = write_packet(0x7fe0, data);
            CHECK_EQ(request(fixture, port, write, 1)[0], data_remainder(data));
            read_block(fixture, port, write, data);
        }
    }
}

TEST(controller_pak_short_and_excess_write_data_preserve_block_boundaries) {
    PakFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (u16 address : {u16{0}, u16{0x20}, u16{0x7fe0}, u16{0x8000}, u16{0xffe0}})
            for (unsigned length = 1; length <= 40; ++length)
                for (bool rejected : {false, true}) {
                    for (unsigned other = 0; other < 4; ++other)
                        fixture.bus.controller_paks[other].fill(static_cast<u8>(0xa0 + other));
                    auto expected = fixture.bus.controller_paks;
                    const auto header = write_packet(address, {});
                    std::vector<u8> input(header.begin(), header.begin() + 3);
                    if (rejected)
                        input[2] ^= 1;
                    for (unsigned index = 0; index < length; ++index)
                        input.push_back(static_cast<u8>(index * 17 + 3));
                    const unsigned written = std::min(length, 32U);
                    if (!rejected && address < 0x8000)
                        std::copy_n(input.begin() + 3, written, expected[port].begin() + address);
                    const auto crc =
                        length < 32 ? 0U : data_remainder(std::span<const u8, 32>(input.data() + 3, 32));
                    const auto reply = request(fixture, port, input, 4);
                    CHECK_EQ(reply[0], crc ^ (rejected ? 0xffU : 0U));
                    CHECK_EQ(reply[1], 0U);
                    CHECK_EQ(reply[2], 0U);
                    CHECK_EQ(reply[3], 0U);
                    CHECK(fixture.bus.controller_paks == expected);
                }
}

TEST(controller_pak_read_prefixes_and_padding_at_storage_boundaries) {
    PakFixture fixture;
    fixture.ready();
    for (unsigned port = 0; port < 4; ++port)
        for (unsigned index = 0; index < 0x8000; ++index)
            fixture.bus.controller_paks[port][index] = static_cast<u8>(index + (index >> 8) + port * 13);
    const auto storage = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (u16 address : {u16{0}, u16{0x7fe0}, u16{0x8000}, u16{0xffe0}})
            for (u8 receive = 1; receive <= 40; ++receive)
                for (bool rejected : {false, true}) {
                    const auto write = write_packet(address, {});
                    std::array<u8, 3> input{2, write[1], write[2]};
                    if (rejected)
                        input[2] ^= 1;
                    std::array<u8, 32> data{};
                    if (!rejected && address < 0x8000)
                        std::copy_n(storage[port].begin() + address, 32, data.begin());
                    std::array<u8, 40> expected{};
                    std::copy(data.begin(), data.end(), expected.begin());
                    expected[32] = data_remainder(data) ^ (rejected ? 0xffU : 0U);
                    const auto reply = request(fixture, port, input, receive);
                    CHECK(std::equal(reply.begin(), reply.end(), expected.begin()));
                    CHECK(fixture.bus.controller_paks == storage);
                }
}

TEST(controller_pak_malformed_lengths_preserve_storage_and_set_no_response) {
    PakFixture fixture;
    fixture.ready();
    const auto storage = fixture.bus.controller_paks;
    for (unsigned port = 0; port < 4; ++port)
        for (bool write : {false, true})
            for (u8 send = 1; send <= (write ? 35 : 3); ++send)
                for (u8 receive : {u8{0}, u8{1}}) {
                    if (receive && send >= (write ? 4 : 3))
                        continue;
                    std::array<u8, 35> input{};
                    input[0] = write ? 3 : 2;
                    fixture.packet(port, std::span<const u8>(input).first(send), receive);
                    const auto reply = fixture.execute(receive);
                    CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive | 0x80U);
                    for (u8 byte : reply)
                        CHECK_EQ(byte, 0xccU);
                    CHECK_EQ(fixture.bus.pif[fixture.response + receive], 0xfeU);
                    CHECK(fixture.bus.controller_paks == storage);
                }
}

TEST(controller_pak_dma_transfers_last_block_at_completion_on_all_ports) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read_dma : {false, true})
            for (bool cpu_clock : {false, true})
                for (bool single : {false, true}) {
                    PakFixture fixture;
                    test::initialize_memory(fixture.system);
                    fixture.ready();
                    for (auto& pak : fixture.bus.controller_paks)
                        pak.fill(0xa5);
                    std::array<u8, 32> data{};
                    for (unsigned index = 0; index < data.size(); ++index)
                        data[index] = static_cast<u8>(port * 37 + index);
                    const auto write = write_packet(0x7fe0, data);
                    auto expected = fixture.bus.controller_paks;
                    u64 cpu_fraction = 0;
                    for (bool read_command : {false, true}) {
                        const std::array<u8, 3> read{2, write[1], write[2]};
                        const u8 receive = read_command ? 33 : 1;
                        fixture.packet(port,
                                       read_command ? std::span<const u8>(read) : std::span<const u8>(write),
                                       receive);
                        fixture.bus.pif[0x7ff] = read_dma ? 0 : 1;
                        if (!read_dma) {
                            for (u32 index = 0; index < 64; ++index)
                                fixture.bus.write_ram_byte(0x2000 + index, fixture.bus.pif[0x7c0 + index]);
                            std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
                        }
                        fixture.bus.write(0x04800000, 4, 0x2000);
                        fixture.bus.write(read_dma ? 0x04800004 : 0x04800010, 4, 0x1fc007c0);
                        const u64 cycles = read_dma ? 37020 + port * 1420 : 4065;
                        const u64 elapsed = cpu_clock ? (cycles * 3 - cpu_fraction + 1) / 2 : cycles;
                        if (cpu_clock)
                            cpu_fraction = (cpu_fraction + elapsed * 2) % 3;
                        const auto advance = [&](u64 amount) {
                            if (cpu_clock)
                                fixture.system.advance(amount);
                            else
                                fixture.bus.tick(amount);
                        };
                        if (single) {
                            for (u64 index = 0; index < elapsed - 1; ++index)
                                advance(1);
                        } else {
                            advance(elapsed - 1);
                        }
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 1U);
                        CHECK(fixture.bus.controller_paks == expected);
                        advance(1);
                        CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                        std::copy(data.begin(), data.end(), expected[port].begin() + 0x7fe0);
                        CHECK(fixture.bus.controller_paks == expected);
                        CHECK_EQ(fixture.bus.pif[0x7c1 + port], receive);
                        CHECK_EQ(fixture.bus.pif[fixture.response + receive - 1], data_remainder(data));
                        if (read_command)
                            CHECK(std::equal(data.begin(), data.end(),
                                             fixture.bus.pif.begin() + fixture.response));
                        if (read_dma)
                            for (u32 index = 0; index < 64; ++index)
                                CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index),
                                         fixture.bus.pif[0x7c0 + index]);
                        fixture.bus.write(0x04800018, 4, 0);
                    }
                    fixture.system.reset();
                    CHECK(fixture.bus.controller_paks == expected);
                }
}
