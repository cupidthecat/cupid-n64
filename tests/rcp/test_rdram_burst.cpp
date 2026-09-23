#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <memory>

using namespace cupid;

TEST(rdram_cache_bursts_preserve_bytes_hidden_bits_and_bank_state) {
    auto system = std::make_unique<System>();
    for (const u32 size : {0x400000U, 0x800000U}) {
        system->bus.rdram.resize(size);
        test::initialize_memory(*system);
        auto& bus = system->bus;
        auto& memory = bus.memory;
        CHECK(memory.direct_access_ready());
        for (const unsigned width : {16U, 32U}) {
            std::array<u8, 32> input{};
            for (unsigned index = 0; index < width; ++index)
                input[index] = static_cast<u8>(index * 23U + index / 2U);
            for (u32 bank = 0; bank < size / 0x100000U; ++bank) {
                for (const u32 offset : {0U, 0x800U - width, 0x800U, 0x100000U - width}) {
                    const u32 address = bank * 0x100000U + offset;
                    std::fill(bus.rdram.begin(), bus.rdram.end(), u8{0xcc});
                    memory.invalidate_banks();
                    memory.advance_clock(17);
                    CHECK(bus.write_cache(address, std::span(input).first(width)));
                    CHECK(std::equal(input.begin(), input.begin() + width, bus.rdram.begin() + address));
                    if (address != 0)
                        CHECK_EQ(bus.rdram[address - 1U], 0xccU);
                    if (address + width != size)
                        CHECK_EQ(bus.rdram[address + width], 0xccU);
                    for (unsigned pair = 0; pair < width / 2U; ++pair)
                        CHECK_EQ(memory.hidden_pair(address + pair * 2U), (input[pair * 2U + 1U] & 1U) * 3U);
                    CHECK_EQ(memory.bank_status(), 0xff00U | (1U << bank));
                    CHECK(memory.row_open(address));
                    CHECK_EQ(memory.bank_access_clock(address), memory.clock());
                    memory.advance_clock(13);
                    std::array<u8, 34> output{};
                    output.fill(0xa5U);
                    CHECK(bus.read_cache(address, std::span(output).subspan(1, width)));
                    CHECK(std::equal(input.begin(), input.begin() + width, output.begin() + 1));
                    CHECK_EQ(output[0], 0xa5U);
                    CHECK_EQ(output[width + 1U], 0xa5U);
                    CHECK_EQ(memory.bank_status(), 0xff00U | (1U << bank));
                    CHECK_EQ(memory.bank_access_clock(address), memory.clock());
                    CHECK_EQ(memory.errors(), 0U);
                }
            }
        }
    }
}

TEST(rdram_cache_burst_fallback_matches_word_transfers_and_calibration_noise) {
    auto burst = std::make_unique<System>();
    auto words = std::make_unique<System>();
    for (unsigned mode = 0; mode < 5; ++mode) {
        for (auto* system : {burst.get(), words.get()}) {
            system->bus.rdram.resize(0x400000U);
            test::initialize_memory(*system);
            auto& memory = system->bus.memory;
            if (mode == 1)
                memory.set_bus_active(false);
            if (mode == 2)
                memory.write_register(0x03f00004U, 10U << 26U);
            if (mode >= 3) {
                // CCI 11 lies between chip zero's low and high thresholds.
                const unsigned encoded = (mode == 3 ? 11U : 0U) ^ 63U;
                constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
                u32 control = 0x02000000U;
                for (unsigned bit = 0; bit < positions.size(); ++bit)
                    control |= ((encoded >> bit) & 1U) << positions[bit];
                memory.write_register(0x03f0000cU, control);
            }
        }
        const u32 base = mode == 2 ? 0x00a00000U : 0;
        for (const unsigned width : {16U, 32U}) {
            // Include unaligned words, row/chip boundaries, absent chips and range errors.
            for (const u32 offset : {0x100U, 0x103U, 0x7fcU, 0x1ffffcU, 0x3ffffcU, 0x800000U}) {
                const u32 address = base + offset;
                std::array<u8, 32> input{};
                input.fill(0xffU);
                burst->bus.memory.advance_clock(23);
                words->bus.memory.advance_clock(23);
                burst->bus.memory.write_burst(address, std::span(input).first(width));
                for (unsigned index = 0; index < width; index += 4)
                    words->bus.memory.write(address + index, 4, read_be32(input.data() + index));
                std::array<u8, 32> actual{}, expected{};
                burst->bus.memory.read_burst(address, std::span(actual).first(width));
                for (unsigned index = 0; index < width; index += 4)
                    write_be32(expected.data() + index,
                               static_cast<u32>(words->bus.memory.read(address + index, 4)));
                CHECK_EQ(actual, expected);
                CHECK_EQ(burst->bus.rdram, words->bus.rdram);
                CHECK(std::equal(burst->bus.memory.hidden_memory().begin(),
                                 burst->bus.memory.hidden_memory().end(),
                                 words->bus.memory.hidden_memory().begin()));
                CHECK_EQ(burst->bus.memory.bank_status(), words->bus.memory.bank_status());
                CHECK_EQ(burst->bus.memory.errors(), words->bus.memory.errors());
                for (u32 bank = 0; bank < 8; ++bank) {
                    const u32 bank_address = bank << 20U;
                    CHECK_EQ(burst->bus.memory.bank_access_clock(bank_address),
                             words->bus.memory.bank_access_clock(bank_address));
                    for (u32 row = 0; row < 512; ++row)
                        CHECK_EQ(burst->bus.memory.row_open(bank_address + (row << 11U)),
                                 words->bus.memory.row_open(bank_address + (row << 11U)));
                }
            }
        }
    }
}

TEST(rdram_cache_burst_access_scopes_retain_first_and_last_rows) {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    auto& memory = system->bus.memory;
    Rdram::BankAccessSummary summary;
    std::array<u8, 32> bytes{};
    {
        Rdram::BankAccessScope scope(memory, summary);
        memory.write_burst(0x2007e0U, bytes);
        memory.advance_clock(11);
        memory.read_burst(0x200800U, bytes);
        CHECK_EQ(memory.bank_status(), 0U);
    }
    CHECK(summary.banks[2].visited);
    CHECK(summary.banks[2].changed_row);
    CHECK_EQ(summary.banks[2].first_row, 0U);
    CHECK_EQ(summary.banks[2].last_row, 1U);
    CHECK(!summary.banks[2].dirty);
    memory.merge_bank_accesses(summary);
    CHECK_EQ(memory.bank_status(), 4U);
    CHECK_EQ(memory.bank_access_clock(0x200800U), 11U);
    CHECK(memory.row_open(0x200800U));
}
