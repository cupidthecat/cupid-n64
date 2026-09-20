#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace {
using namespace cupid;
constexpr std::array<u32, 3> capacities{0x8000, 0x18000, 0x20000};

u16 read_half(const u8* data) {
    return static_cast<u16>((data[0] << 8) | data[1]);
}

struct SramFixture {
    System system;
    Bus& bus{system.bus};

    explicit SramFixture(u32 capacity) {
        test::initialize_memory(system);
        bus.set_save_type(SaveType::Sram);
        bus.sram.resize(capacity);
        for (u32 index = 0; index < capacity; ++index)
            bus.sram[index] = static_cast<u8>((index * 17) ^ (index >> 8) ^ (index >> 15));
    }

    void start(u32 address, u32 bytes, bool to_dram, u32 dram = 0x2000) {
        bus.write(0x04600000, 4, dram);
        bus.write(0x04600004, 4, address);
        bus.write(to_dram ? 0x0460000c : 0x04600008, 4, bytes - 1);
    }

    void complete() {
        bus.tick(1000000);
        CHECK_EQ(bus.read(0x04600010, 4), 8U);
        CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0x10U);
        bus.write(0x04600010, 4, 2);
    }
};

struct Window {
    u32 offset;
    u32 remaining;
};

std::optional<Window> window(u32 capacity, u32 address) {
    const u32 relative = address - 0x08000000;
    if (capacity == 0x8000) {
        const u32 offset = relative % 0x8000;
        return Window{offset, 0x8000 - offset};
    }
    for (u32 bank = 0; bank < capacity / 0x8000; ++bank) {
        const u32 start = bank * 0x40000;
        if (relative >= start && relative < start + 0x8000) {
            const u32 offset = relative - start;
            return Window{bank * 0x8000 + offset, 0x8000 - offset};
        }
    }
    return std::nullopt;
}

// Describe each selected page as a responding prefix followed by an open bus tail.
std::vector<u8> read_pages(const std::vector<u8>& storage, u32 address, u32 bytes, u32 page) {
    std::vector<u8> result(bytes);
    for (u32 copied = 0; copied < bytes;) {
        const u32 selected = address + copied;
        const u32 segment = std::min(bytes - copied, page - selected % page);
        const auto memory = window(static_cast<u32>(storage.size()), selected);
        const u32 driven = memory ? std::min(segment, memory->remaining) : 0;
        if (driven)
            std::copy_n(storage.begin() + memory->offset, driven, result.begin() + copied);
        const u16 tail =
            driven ? static_cast<u16>((result[copied + driven - 2] << 8) | result[copied + driven - 1])
                   : static_cast<u16>(selected);
        for (u32 index = driven; index < segment; index += 2) {
            result[copied + index] = static_cast<u8>(tail >> 8);
            result[copied + index + 1] = static_cast<u8>(tail);
        }
        copied += segment;
    }
    return result;
}

void write_pages(std::vector<u8>& storage, u32 address, std::span<const u8> data, u32 page) {
    for (u32 copied = 0; copied < data.size();) {
        const u32 selected = address + copied;
        const u32 segment = std::min(static_cast<u32>(data.size()) - copied, page - selected % page);
        const auto memory = window(static_cast<u32>(storage.size()), selected);
        if (memory)
            std::copy_n(data.begin() + copied, std::min(segment, memory->remaining),
                        storage.begin() + memory->offset);
        copied += segment;
    }
}
} // namespace

TEST(sram_cpu_word_access_covers_every_bank_and_preserves_reset_contents) {
    for (u32 capacity : capacities) {
        SramFixture fixture(capacity);
        auto expected = fixture.bus.sram;
        for (u32 offset = 0; offset < capacity; offset += 4) {
            const u32 address = 0x08000000 + (offset / 0x8000) * 0x40000 + offset % 0x8000;
            const u32 value = 0x91234567U ^ (offset * 0x10203U);
            fixture.bus.write(address, 4, value);
            CHECK_EQ(fixture.bus.read(0x04600010, 4), 2U);
            CHECK_EQ(fixture.bus.read(0x05000000, 4), value);
            CHECK_EQ(fixture.bus.read(0x04600010, 4), 0U);
            write_be32(expected.data() + offset, value);
        }
        CHECK(fixture.bus.sram == expected);
        for (u32 offset = 0; offset < capacity; offset += 4) {
            const u32 address = 0x08000000 + (offset / 0x8000) * 0x40000 + offset % 0x8000;
            CHECK_EQ(fixture.bus.read(address, 4), read_be32(expected.data() + offset));
        }
        fixture.system.reset();
        CHECK_EQ(fixture.bus.save_type, SaveType::Sram);
        CHECK(fixture.bus.sram == expected);
        fixture.bus.set_save_type(SaveType::Sram);
        CHECK(fixture.bus.sram == expected);
    }
}

TEST(sram_cpu_mirrors_bank_windows_and_holes_cover_the_entire_save_address_region) {
    for (u32 capacity : capacities) {
        SramFixture fixture(capacity);
        const auto original = fixture.bus.sram;
        for (u32 window_index = 0; window_index < 4096; ++window_index)
            for (u32 offset : {0U, 0x120U, 0x7ffcU, 0x7ffeU}) {
                const u32 address = 0x08000000 + window_index * 0x8000 + offset;
                const auto memory = window(capacity, address);
                const u16 first =
                    memory ? read_half(original.data() + memory->offset) : static_cast<u16>(address);
                const u16 second = memory && memory->remaining >= 4
                                       ? read_half(original.data() + memory->offset + 2)
                                       : first;
                CHECK_EQ(fixture.bus.read(address, 4), (static_cast<u32>(first) << 16) | second);
            }
        CHECK(fixture.bus.sram == original);
    }
}

TEST(sram_cpu_writes_to_bank_holes_and_absent_banks_only_update_the_pi_latch) {
    for (u32 capacity : {0x18000U, 0x20000U}) {
        SramFixture fixture(capacity);
        const auto original = fixture.bus.sram;
        for (u32 window_index = 0; window_index < 4096; ++window_index) {
            const u32 address = 0x08000000 + window_index * 0x8000;
            if (window(capacity, address))
                continue;
            const u32 value = 0xa5000000 | window_index;
            fixture.bus.write(address, 4, value);
            CHECK_EQ(fixture.bus.read(0x04600034, 4), value);
            CHECK_EQ(fixture.bus.read(address, 4), value);
            CHECK_EQ(fixture.bus.read(address, 4), (address & 0xffffU) * 0x10001U);
        }
        CHECK(fixture.bus.sram == original);
    }
}

TEST(sram_subword_cpu_stores_drive_a_full_word_and_respect_the_selected_window_end) {
    for (u32 capacity : capacities)
        for (u32 bank = 0; bank < capacity / 0x8000; ++bank)
            for (u32 offset : {0U, 0x7ffcU})
                for (unsigned width : {1U, 2U})
                    for (u32 lane = 0; lane < 4; lane += width) {
                        SramFixture fixture(capacity);
                        auto expected = fixture.bus.sram;
                        const u32 address = 0x08000000 + bank * 0x40000 + offset + lane;
                        const u32 value = width == 1 ? 0xab : 0xabcd;
                        const u32 word = value << ((4 - width - lane) * 8);
                        const u32 selected = bank * 0x8000 + offset + (lane & ~1U);
                        std::array<u8, 4> bytes{};
                        write_be32(bytes.data(), word);
                        const u32 count = std::min(4U, (bank + 1) * 0x8000 - selected);
                        std::copy_n(bytes.begin(), count, expected.begin() + selected);
                        fixture.bus.write(address, width, value);
                        CHECK(fixture.bus.sram == expected);
                        CHECK_EQ(fixture.bus.read(0x05000000, 4), word);
                        CHECK_EQ(fixture.bus.read(0x04600010, 4), 0U);
                    }
}

TEST(sram_subword_cpu_loads_select_lanes_from_two_halfword_bus_beats) {
    for (u32 capacity : capacities) {
        SramFixture fixture(capacity);
        const auto original = fixture.bus.sram;
        for (u32 bank = 0; bank < capacity / 0x8000; ++bank)
            for (u32 offset : {0U, 0x7ffcU, 0x8000U})
                for (unsigned width : {1U, 2U})
                    for (u32 lane = 0; lane < 4; lane += width) {
                        const u32 address = 0x08000000 + bank * 0x40000 + offset + lane;
                        const auto memory = window(capacity, address & ~1U);
                        const u16 first = memory ? read_half(original.data() + memory->offset)
                                                 : static_cast<u16>(address & ~1U);
                        const u16 second = memory && memory->remaining >= 4
                                               ? read_half(original.data() + memory->offset + 2)
                                               : first;
                        const u32 word = (static_cast<u32>(first) << 16) | second;
                        const u32 mask = width == 1 ? 0xff : 0xffff;
                        CHECK_EQ(fixture.bus.read(address, width), (word >> ((4 - width - lane) * 8)) & mask);
                        CHECK_EQ(fixture.bus.read(0x04600004, 4), (address + 4) & ~1U);
                        CHECK_EQ(fixture.bus.read(0x04600034, 4), word);
                    }
        CHECK(fixture.bus.sram == original);
    }
}

TEST(sram_dma_page_sizes_boundaries_and_banks_match_selected_memory_windows) {
    for (u32 capacity : capacities) {
        SramFixture fixture(capacity);
        for (u32 bank = 0; bank <= capacity / 0x8000; ++bank)
            for (u32 offset : {0U, 0x7ffcU, 0x8000U, 0x3fffcU})
                for (u32 pgs = 0; pgs < 16; ++pgs)
                    for (bool to_dram : {false, true}) {
                        const u32 address = 0x08000000 + bank * 0x40000 + offset;
                        const u32 bytes = 132;
                        const u32 page = 1U << (pgs + 2);
                        fixture.bus.write(0x0460002c, 4, pgs);
                        auto expected_storage = fixture.bus.sram;
                        std::vector<u8> data(bytes);
                        for (u32 index = 0; index < bytes; ++index) {
                            data[index] = static_cast<u8>(index * 19 + bank * 31 + pgs);
                            fixture.bus.write_ram_byte(0x2000 + index, to_dram ? 0xa5 : data[index]);
                        }
                        if (to_dram)
                            data = read_pages(expected_storage, address, bytes, page);
                        else
                            write_pages(expected_storage, address, data, page);
                        fixture.start(address, bytes, to_dram);
                        fixture.complete();
                        CHECK(fixture.bus.sram == expected_storage);
                        for (u32 index = 0; index < bytes; ++index)
                            CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index), data[index]);
                        const u16 tail = static_cast<u16>((data[bytes - 2] << 8) | data[bytes - 1]);
                        CHECK_EQ(fixture.bus.read(0x04600034, 4), static_cast<u32>(tail) * 0x10001U);
                    }
    }
}

TEST(sram_large_dma_roundtrips_keep_other_banks_and_rdram_guards_intact) {
    for (u32 capacity : capacities) {
        SramFixture fixture(capacity);
        fixture.bus.write(0x0460002c, 4, 5);
        auto expected = fixture.bus.sram;
        for (u32 bank = 0; bank < capacity / 0x8000; ++bank) {
            const u32 address = 0x08000000 + bank * 0x40000;
            for (u32 index = 0; index < 0x8000; ++index) {
                const u8 value = static_cast<u8>((index * 7) ^ (bank * 43) ^ (index >> 7));
                fixture.bus.write_ram_byte(0x2000 + index, value);
                expected[bank * 0x8000 + index] = value;
            }
            fixture.start(address, 0x8000, false);
            fixture.complete();
            CHECK(fixture.bus.sram == expected);
            for (u32 index = 0x1ff8; index < 0xa008; ++index)
                fixture.bus.write_ram_byte(index, 0xcc);
            fixture.start(address, 0x8000, true);
            fixture.complete();
            for (u32 index = 0; index < 0x8000; ++index)
                CHECK_EQ(fixture.bus.read_ram_byte(0x2000 + index), expected[bank * 0x8000 + index]);
            for (u32 index = 0; index < 8; ++index) {
                CHECK_EQ(fixture.bus.read_ram_byte(0x1ff8 + index), 0xccU);
                CHECK_EQ(fixture.bus.read_ram_byte(0xa000 + index), 0xccU);
            }
            CHECK(fixture.bus.sram == expected);
        }
        fixture.system.reset();
        CHECK(fixture.bus.sram == expected);
    }
}

TEST(sram_cpu_io_busy_writes_and_clock_steps_preserve_storage) {
    for (u32 capacity : capacities)
        for (bool cpu_clock : {false, true})
            for (bool single : {false, true}) {
                SramFixture fixture(capacity);
                auto expected = fixture.bus.sram;
                const u32 bank = capacity / 0x8000 - 1;
                const u32 address = 0x08000000 + bank * 0x40000 + 0x7ffc;
                fixture.bus.write(address, 4, 0x12345678);
                write_be32(expected.data() + bank * 0x8000 + 0x7ffc, 0x12345678);
                fixture.bus.write(address, 4, 0xabcdef01);
                CHECK(fixture.bus.sram == expected);
                const u64 cycles = cpu_clock ? 210 : 140;
                const auto advance = [&](u64 amount) {
                    if (cpu_clock)
                        fixture.system.advance(amount);
                    else
                        fixture.bus.tick(amount);
                };
                if (single) {
                    for (u64 index = 0; index < cycles - 1; ++index)
                        advance(1);
                } else {
                    advance(cycles - 1);
                }
                CHECK_EQ(fixture.bus.read(0x04600010, 4), 2U);
                advance(1);
                CHECK_EQ(fixture.bus.read(0x04600010, 4), 0U);
                CHECK(fixture.bus.sram == expected);
                fixture.bus.write(address, 4, 0xabcdef01);
                write_be32(expected.data() + bank * 0x8000 + 0x7ffc, 0xabcdef01);
                CHECK(fixture.bus.sram == expected);
                fixture.system.reset();
                CHECK(fixture.bus.sram == expected);
                CHECK_EQ(fixture.bus.read(0x04600010, 4), 0U);
            }
}
