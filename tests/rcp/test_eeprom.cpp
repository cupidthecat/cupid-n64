#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace {
using namespace cupid;

constexpr std::array<SaveType, 2> types{SaveType::Eeprom4K, SaveType::Eeprom16K};
constexpr u64 write_cycles = 375000;
constexpr u64 read_dma_cycles = 39280;

struct EepromFixture {
    System system;
    Bus& bus{system.bus};
    u32 output_offset{};

    explicit EepromFixture(SaveType type) {
        test::initialize_memory(system);
        bus.set_save_type(type);
        bus.write(0x04700010, 4, 0);
    }

    void packet(std::span<const u8> input, u8 receive) {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        bus.pif[0x7c4] = static_cast<u8>(input.size());
        bus.pif[0x7c5] = receive;
        std::copy(input.begin(), input.end(), bus.pif.begin() + 0x7c6);
        output_offset = 0x7c6 + static_cast<u32>(input.size());
        std::fill_n(bus.pif.begin() + output_offset, receive, u8{0xcc});
        bus.pif[output_offset + receive] = 0xfe;
    }

    std::vector<u8> command(std::initializer_list<u8> input, u8 receive) {
        packet(std::span<const u8>(input.begin(), input.size()), receive);
        return execute(receive);
    }

    std::vector<u8> execute(u8 receive) {
        bus.write(0x1fc007fc, 4, 1);
        static_cast<void>(bus.read(0x1fc007fc, 4));
        return {bus.pif.begin() + output_offset, bus.pif.begin() + output_offset + receive};
    }

    void start_dma() {
        bus.write(0x04800000, 4, 0x2000);
        bus.write(0x04800004, 4, 0x1fc007c0);
    }

    u8 busy() {
        return command({0x00}, 3)[2];
    }
};
} // namespace

TEST(eeprom_block_reads_decode_only_the_installed_address_bits) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        for (unsigned index = 0; index < fixture.bus.eeprom.size(); ++index)
            fixture.bus.eeprom[index] = static_cast<u8>((index / 8) ^ (index % 8 * 19));
        for (unsigned block = 0; block < 256; ++block) {
            const auto data = fixture.command({0x04, static_cast<u8>(block)}, 8);
            CHECK_EQ(fixture.bus.pif[0x7c5], 8U);
            for (unsigned index = 0; index < data.size(); ++index)
                CHECK_EQ(data[index], fixture.bus.eeprom[(block * 8 + index) % fixture.bus.eeprom.size()]);
        }
    }
}

TEST(eeprom_block_writes_alias_on_small_chips_and_remain_distinct_on_large_chips) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        for (unsigned block = 0; block < 256; ++block) {
            const u8 value = static_cast<u8>(block);
            CHECK_EQ(
                fixture.command({0x05, value, value, value, value, value, value, value, value, value}, 1)[0],
                0U);
            fixture.bus.tick(write_cycles);
        }
        for (unsigned index = 0; index < fixture.bus.eeprom.size(); ++index)
            CHECK_EQ(fixture.bus.eeprom[index], type == SaveType::Eeprom4K ? 192U + index / 8 : index / 8);
    }
}

TEST(eeprom_long_transfers_wrap_at_the_chip_boundary) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        const u8 last_block = type == SaveType::Eeprom4K ? 63 : 255;
        std::array<u8, 26> input{0x05, last_block};
        for (unsigned index = 2; index < input.size(); ++index)
            input[index] = static_cast<u8>(index * 7);
        fixture.packet(input, 1);
        CHECK_EQ(fixture.execute(1)[0], 0U);
        fixture.bus.tick(write_cycles);
        const auto data = fixture.command({0x04, last_block}, 24);
        for (unsigned index = 0; index < data.size(); ++index) {
            CHECK_EQ(data[index], input[index + 2]);
            CHECK_EQ(fixture.bus.eeprom[(fixture.bus.eeprom.size() - 8 + index) % fixture.bus.eeprom.size()],
                     input[index + 2]);
        }
    }
}

TEST(eeprom_status_returns_the_requested_prefix_without_overwriting_packet_bytes) {
    for (SaveType type : types)
        for (u8 command : {u8{0x00}, u8{0xff}})
            for (u8 receive = 0; receive <= 4; ++receive) {
                EepromFixture fixture(type);
                const auto data = fixture.command({command}, receive);
                CHECK_EQ(fixture.bus.pif[0x7c5], receive);
                const std::array<u8, 4> expected{0, type == SaveType::Eeprom4K ? u8{0x80} : u8{0xc0}, 0, 0};
                for (unsigned index = 0; index < receive; ++index)
                    CHECK_EQ(data[index], expected[index]);
                CHECK_EQ(fixture.bus.pif[fixture.output_offset + receive], 0xfeU);
            }
}

TEST(eeprom_busy_writes_and_reset_commands_preserve_the_original_deadline_and_payload) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        CHECK_EQ(fixture.command({0x05, 3, 1, 2, 3, 4, 5, 6, 7, 8}, 1)[0], 0U);
        fixture.bus.tick(1000);
        CHECK_EQ(fixture.command({0x05, 3, 9, 9, 9, 9, 9, 9, 9, 9}, 1)[0], 0x80U);
        CHECK_EQ(fixture.command({0xff}, 3)[2], 0x80U);
        for (u8 value : fixture.command({0x04, 3}, 8))
            CHECK_EQ(value, 0xffU);
        fixture.bus.tick(write_cycles - 1001);
        CHECK_EQ(fixture.busy(), 0x80U);
        fixture.bus.tick(1);
        CHECK_EQ(fixture.busy(), 0U);
        CHECK_EQ(fixture.bus.read(0x04300008, 4) & 2U, 0U);
        const auto data = fixture.command({0x04, 3}, 8);
        for (unsigned index = 0; index < data.size(); ++index)
            CHECK_EQ(data[index], index + 1U);
    }
}

TEST(eeprom_si_dma_starts_a_full_write_interval_under_bulk_and_single_cycle_advances) {
    for (SaveType type : types)
        for (bool cpu_clock : {false, true})
            for (bool single_cycle : {false, true}) {
                EepromFixture fixture(type);
                u64 total_rcp = 0;
                u64 total_cpu = 0;
                const auto advance = [&](u64 cycles) {
                    total_rcp += cycles;
                    const u64 next_cpu = (total_rcp * 3 + 1) / 2;
                    const u64 elapsed = cpu_clock ? next_cpu - total_cpu : cycles;
                    total_cpu = next_cpu;
                    if (single_cycle) {
                        for (u64 index = 0; index < elapsed; ++index) {
                            if (cpu_clock)
                                fixture.system.advance(1);
                            else
                                fixture.bus.tick(1);
                        }
                    } else if (cpu_clock) {
                        fixture.system.advance(elapsed);
                    } else {
                        fixture.bus.tick(cycles);
                    }
                };
                const std::array<u8, 10> input{0x05, 3, 1, 2, 3, 4, 5, 6, 7, 8};
                fixture.packet(input, 1);
                fixture.start_dma();
                advance(read_dma_cycles - 1);
                CHECK_EQ(fixture.bus.eeprom[24], 0xffU);
                advance(1);
                CHECK_EQ(fixture.bus.read_ram_byte(0x2010), 0U);
                CHECK_EQ(fixture.bus.eeprom[24], 1U);
                CHECK_EQ(fixture.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                advance(write_cycles - 1);
                CHECK_EQ(fixture.busy(), 0x80U);
                advance(1);
                CHECK_EQ(fixture.busy(), 0U);
            }
}

TEST(eeprom_completion_precedes_a_new_si_command_at_the_same_clock) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        fixture.command({0x05, 0, 0x12}, 1);
        fixture.bus.tick(write_cycles - read_dma_cycles);
        const std::array<u8, 3> input{0x05, 1, 0x34};
        fixture.packet(input, 1);
        fixture.start_dma();
        fixture.bus.tick(read_dma_cycles);
        CHECK_EQ(fixture.bus.read_ram_byte(0x2009), 0U);
        CHECK_EQ(fixture.bus.eeprom[0], 0x12U);
        CHECK_EQ(fixture.bus.eeprom[8], 0x34U);
        fixture.bus.tick(write_cycles - 1);
        CHECK_EQ(fixture.busy(), 0x80U);
        fixture.bus.tick(1);
        CHECK_EQ(fixture.busy(), 0U);
    }
}

TEST(eeprom_reset_cancels_busy_time_and_preserves_the_array) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        fixture.command({0x05, 1, 0x12, 0x34}, 1);
        CHECK_EQ(fixture.busy(), 0x80U);
        fixture.system.reset();
        CHECK_EQ(fixture.busy(), 0U);
        const auto data = fixture.command({0x04, 1}, 8);
        CHECK_EQ(data[0], 0x12U);
        CHECK_EQ(data[1], 0x34U);
        for (unsigned index = 2; index < data.size(); ++index)
            CHECK_EQ(data[index], 0xffU);
    }
}

TEST(eeprom_absent_or_unsupported_arrays_do_not_respond_or_change_storage) {
    for (SaveType type : {SaveType::None, SaveType::Sram, SaveType::FlashRam}) {
        EepromFixture fixture(type);
        fixture.bus.eeprom.assign(512, 0x3c);
        for (u8 command : {u8{0x00}, u8{0xff}, u8{0x04}, u8{0x05}}) {
            fixture.command({command, 0, 0x12}, 3);
            CHECK_EQ(fixture.bus.pif[0x7c5], 0x83U);
            CHECK(std::all_of(fixture.bus.eeprom.begin(), fixture.bus.eeprom.end(),
                              [](u8 value) { return value == 0x3c; }));
        }
    }
    for (std::size_t size : {0U, 511U, 513U, 2049U}) {
        EepromFixture fixture(SaveType::Eeprom4K);
        fixture.bus.eeprom.assign(size, 0x3c);
        fixture.command({0x00}, 3);
        CHECK_EQ(fixture.bus.pif[0x7c5], 0x83U);
        fixture.command({0x05, 0, 0x12}, 1);
        CHECK_EQ(fixture.bus.pif[0x7c5], 0x81U);
        CHECK(std::all_of(fixture.bus.eeprom.begin(), fixture.bus.eeprom.end(),
                          [](u8 value) { return value == 0x3c; }));
    }
}

TEST(eeprom_command_lengths_reject_missing_fields_and_allow_empty_data) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        for (u8 command : {u8{0x04}, u8{0x05}, u8{0x06}}) {
            fixture.command({command}, 1);
            CHECK_EQ(fixture.bus.pif[0x7c5], 0x81U);
        }
        fixture.command({0x05, 0, 0x12}, 0);
        CHECK_EQ(fixture.bus.pif[0x7c5], 0x80U);
        CHECK_EQ(fixture.busy(), 0U);
        CHECK_EQ(fixture.bus.eeprom[0], 0xffU);
        fixture.command({0x04, 0}, 0);
        CHECK_EQ(fixture.bus.pif[0x7c5], 0U);
        CHECK_EQ(fixture.command({0x05, 0}, 1)[0], 0U);
        fixture.bus.tick(write_cycles - 1);
        CHECK_EQ(fixture.busy(), 0x80U);
        fixture.bus.tick(1);
        CHECK_EQ(fixture.busy(), 0U);
        CHECK(std::all_of(fixture.bus.eeprom.begin(), fixture.bus.eeprom.end(),
                          [](u8 value) { return value == 0xff; }));
        for (u8 receive = 1; receive <= 8; ++receive) {
            auto reply = fixture.command({0x05, 0}, receive);
            CHECK_EQ(fixture.bus.pif[0x7c5], receive);
            for (u8 value : reply)
                CHECK_EQ(value, 0U);
            reply = fixture.command({0x05, 0}, receive);
            CHECK_EQ(reply[0], 0x80U);
            for (unsigned index = 1; index < reply.size(); ++index)
                CHECK_EQ(reply[index], 0U);
            CHECK_EQ(fixture.bus.pif[fixture.output_offset + receive], 0xfeU);
            fixture.bus.tick(write_cycles);
        }
    }
}

TEST(eeprom_si_write_dma_executes_the_packet_and_preserves_the_complete_busy_interval) {
    for (SaveType type : types) {
        EepromFixture fixture(type);
        const std::array<u8, 10> input{0x05, 0xff, 1, 2, 3, 4, 5, 6, 7, 8};
        fixture.packet(input, 1);
        fixture.bus.pif[0x7ff] = 1;
        for (u32 index = 0; index < 64; ++index)
            fixture.bus.write_ram_byte(0x2000 + index, fixture.bus.pif[0x7c0 + index]);
        std::fill(fixture.bus.pif.begin() + 0x7c0, fixture.bus.pif.end(), u8{0});
        fixture.bus.write(0x04800000, 4, 0x2000);
        fixture.bus.write(0x04800010, 4, 0x1fc007c0);
        fixture.bus.tick(4064);
        CHECK_EQ(fixture.bus.eeprom.back(), 0xffU);
        fixture.bus.tick(1);
        CHECK_EQ(fixture.bus.eeprom.back(), 8U);
        CHECK_EQ(fixture.bus.pif[0x7d0], 0U);
        fixture.bus.tick(write_cycles - 1);
        CHECK_EQ(fixture.busy(), 0x80U);
        fixture.bus.tick(1);
        CHECK_EQ(fixture.busy(), 0U);
        const std::array<u8, 2> read{0x04, 0xff};
        fixture.packet(read, 8);
        fixture.start_dma();
        fixture.bus.tick(read_dma_cycles);
        for (u32 index = 0; index < 8; ++index)
            CHECK_EQ(fixture.bus.read_ram_byte(0x2008 + index), index + 1);
    }
}
