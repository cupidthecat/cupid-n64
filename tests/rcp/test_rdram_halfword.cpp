#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <memory>

using namespace cupid;

namespace {

void compare_state(const System& cells, const System& separate) {
    CHECK_EQ(cells.bus.rdram, separate.bus.rdram);
    const auto& first = cells.bus.memory;
    const auto& second = separate.bus.memory;
    CHECK(std::equal(first.hidden_memory().begin(), first.hidden_memory().end(),
                     second.hidden_memory().begin()));
    CHECK_EQ(first.bank_status(), second.bank_status());
    CHECK_EQ(first.errors(), second.errors());
    CHECK_EQ(first.clock(), second.clock());
    for (u32 bank = 0; bank < 8; ++bank) {
        const u32 base = bank << 20U;
        CHECK_EQ(first.bank_access_clock(base), second.bank_access_clock(base));
        for (u32 row = 0; row < 512; ++row)
            CHECK_EQ(first.row_open(base + (row << 11U)), second.row_open(base + (row << 11U)));
    }
}

} // namespace

TEST(rdram_halfword_cells_preserve_value_hidden_bits_alignment_and_bank_clocks) {
    auto system = std::make_unique<System>();
    for (const u32 size : {0x400000U, 0x800000U}) {
        system->bus.rdram.resize(size);
        test::initialize_memory(*system);
        auto& memory = system->bus.memory;
        CHECK(memory.direct_access_ready());
        for (u32 bank = 0; bank < size / 0x100000U; ++bank) {
            for (const u32 offset : {0U, 1U, 0x7ffU, 0x800U, 0xfffffU}) {
                const u32 address = (bank << 20U) + offset;
                const u32 aligned = address & ~1U;
                for (u8 hidden = 0; hidden < 8; ++hidden) {
                    const auto value = static_cast<u16>(0x8101U ^ (hidden * 1597U));
                    memory.invalidate_banks();
                    memory.advance_clock(17);
                    memory.write_halfword(address, {value, hidden});
                    CHECK_EQ(system->bus.rdram[aligned], static_cast<u8>(value >> 8U));
                    CHECK_EQ(system->bus.rdram[aligned + 1U], static_cast<u8>(value));
                    CHECK_EQ(memory.hidden_memory()[aligned >> 1U], hidden & 3U);
                    CHECK_EQ(memory.bank_status(), 0xff00U | (1U << bank));
                    CHECK(memory.row_open(address));
                    CHECK_EQ(memory.bank_access_clock(address), memory.clock());
                    memory.advance_clock(11);
                    const auto loaded = memory.read_halfword(address);
                    CHECK_EQ(loaded.value, value);
                    CHECK_EQ(loaded.hidden, hidden & 3U);
                    CHECK_EQ(memory.bank_status(), 0xff00U | (1U << bank));
                    CHECK_EQ(memory.bank_access_clock(address), memory.clock());
                    CHECK_EQ(memory.errors(), 0U);
                }
            }
        }
    }
}

TEST(rdram_halfword_fallback_preserves_mapping_calibration_and_access_errors) {
    auto cells = std::make_unique<System>();
    auto separate = std::make_unique<System>();
    for (unsigned mode = 0; mode < 5; ++mode) {
        for (auto* system : {cells.get(), separate.get()}) {
            system->bus.rdram.resize(0x400000U);
            test::initialize_memory(*system);
            auto& memory = system->bus.memory;
            if (mode == 1)
                memory.set_bus_active(false);
            if (mode == 2)
                memory.write_register(0x03f00004U, 10U << 26U);
            if (mode >= 3) {
                const unsigned encoded = (mode == 3 ? 11U : 0U) ^ 63U;
                constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
                u32 control = 0x02000000U;
                for (unsigned bit = 0; bit < positions.size(); ++bit)
                    control |= ((encoded >> bit) & 1U) << positions[bit];
                memory.write_register(0x03f0000cU, control);
            }
        }
        const u32 base = mode == 2 ? 0x00a00000U : 0U;
        auto& first = cells->bus.memory;
        auto& second = separate->bus.memory;
        for (const u32 offset :
             {0x100U, 0x101U, 0x7ffU, 0x1fffffU, 0x3fffffU, 0x400000U, 0x7ffffeU, 0x800000U, 0x3f00000U}) {
            const u32 address = base + offset;
            for (u8 hidden = 0; hidden < 4; ++hidden) {
                first.clear_error();
                second.clear_error();
                first.advance_clock(23);
                second.advance_clock(23);
                first.write_halfword(address, {0xffffU, hidden});
                second.write(address, 2, 0xffffU);
                second.set_hidden_pair(address, hidden);
                const auto actual = first.read_halfword(address);
                const auto expected = static_cast<u16>(second.read(address, 2));
                CHECK_EQ(actual.value, expected);
                CHECK_EQ(actual.hidden, second.hidden_pair(address));
                // A combined read consumes the same calibration-noise sequence.
                CHECK_EQ(first.read(address, 2), second.read(address, 2));
                CHECK_EQ(first.bank_status(), second.bank_status());
                CHECK_EQ(first.errors(), second.errors());
            }
        }
        compare_state(*cells, *separate);
    }
}

TEST(rdram_halfword_cells_preserve_deferred_raster_bank_order) {
    auto cells = std::make_unique<System>();
    auto separate = std::make_unique<System>();
    test::initialize_memory(*cells);
    test::initialize_memory(*separate);
    auto& first = cells->bus.memory;
    auto& second = separate->bus.memory;
    Rdram::BankAccessSummary summary;
    {
        const Rdram::BankAccessScope scope(first, summary);
        first.write_halfword(0x2007ffU, {0x1234U, 3});
        second.write(0x2007ffU, 2, 0x1234U);
        second.set_hidden_pair(0x2007ffU, 3);
        first.advance_clock(11);
        second.advance_clock(11);
        const auto actual = first.read_halfword(0x200800U);
        CHECK_EQ(actual.value, second.read(0x200800U, 2));
        CHECK_EQ(actual.hidden, second.hidden_pair(0x200800U));
        CHECK_EQ(first.bank_status(), 0U);
    }
    CHECK(summary.banks[2].visited);
    CHECK(summary.banks[2].changed_row);
    CHECK_EQ(summary.banks[2].first_row, 0U);
    CHECK_EQ(summary.banks[2].last_row, 1U);
    CHECK(!summary.banks[2].dirty);
    CHECK_EQ(summary.banks[2].last_access, 11U);
    first.merge_bank_accesses(summary);
    compare_state(*cells, *separate);
}

TEST(rdram_halfword_cells_recheck_a_resized_memory_extent) {
    auto cells = std::make_unique<System>();
    auto separate = std::make_unique<System>();
    for (auto* system : {cells.get(), separate.get()}) {
        test::initialize_memory(*system);
        system->bus.memory.write(0x400000U, 2, 0x1234U);
        system->bus.memory.set_hidden_pair(0x400000U, 2);
        system->bus.rdram.resize(0x400001U);
    }
    auto& first = cells->bus.memory;
    auto& second = separate->bus.memory;
    const auto loaded = first.read_halfword(0x400001U);
    CHECK_EQ(loaded.value, second.read(0x400001U, 2));
    CHECK_EQ(loaded.hidden, second.hidden_pair(0x400001U));
    first.write_halfword(0x400001U, {0xffffU, 1});
    second.write(0x400001U, 2, 0xffffU);
    second.set_hidden_pair(0x400001U, 1);
    CHECK_EQ(cells->bus.rdram.back(), 0x12U);
    compare_state(*cells, *separate);
}
