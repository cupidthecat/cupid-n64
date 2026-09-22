#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <memory>

using namespace cupid;

namespace {
void same_banks(const Rdram& first, const Rdram& second) {
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

TEST(rdram_access_summaries_preserve_dirty_rows_and_timestamps_in_draw_order) {
    for (const bool invalid : {false, true}) {
        auto serial = std::make_unique<System>();
        auto recorded = std::make_unique<System>();
        test::initialize_memory(*serial);
        test::initialize_memory(*recorded);
        for (auto* system : {serial.get(), recorded.get()}) {
            auto& memory = system->bus.memory;
            for (u32 bank = 0; bank < 8; ++bank) {
                memory.advance_clock(7);
                memory.write((bank << 20U) + (3U << 11U), 4, 0xffffffffU);
            }
            if (invalid)
                memory.invalidate_banks();
        }
        std::array<Rdram::BankAccessSummary, 4> summaries{};
        const auto accesses = [](Rdram& memory, unsigned group) {
            for (u32 bank = 0; bank < 8; ++bank) {
                const u32 base = bank << 20U;
                memory.advance_clock(group + 1U);
                (void)memory.read(base + (3U << 11U), 4);
                if (((bank + group) & 1U) == 0)
                    memory.write(base + (3U << 11U) + 4U, 4, group);
                if (((bank + group) & 2U) != 0) {
                    memory.write(base + (4U << 11U), 4, 0xffffffffU);
                    (void)memory.read(base + (3U << 11U), 4);
                }
            }
        };
        for (unsigned group = 0; group < summaries.size(); ++group) {
            accesses(serial->bus.memory, group);
            const Rdram::BankAccessScope scope(recorded->bus.memory, summaries[group]);
            accesses(recorded->bus.memory, group);
        }
        for (const auto& summary : summaries)
            recorded->bus.memory.merge_bank_accesses(summary);
        same_banks(serial->bus.memory, recorded->bus.memory);
        CHECK_EQ(serial->bus.rdram, recorded->bus.rdram);
        CHECK(std::equal(serial->bus.memory.hidden_memory().begin(), serial->bus.memory.hidden_memory().end(),
                         recorded->bus.memory.hidden_memory().begin()));
        // A later refresh must consume the same dirty state as serial accesses.
        CHECK_EQ(serial->bus.memory.refresh_banks(), recorded->bus.memory.refresh_banks());
        same_banks(serial->bus.memory, recorded->bus.memory);
    }
}

TEST(rdram_access_scope_restores_ordinary_tracking_and_keeps_other_machines_independent) {
    auto first = std::make_unique<System>();
    auto second = std::make_unique<System>();
    test::initialize_memory(*first);
    test::initialize_memory(*second);
    Rdram::BankAccessSummary summary;
    {
        const Rdram::BankAccessScope scope(first->bus.memory, summary);
        first->bus.memory.write(0x5000, 4, 1);
        second->bus.memory.write(0x6000, 4, 2);
        CHECK(second->bus.memory.row_open(0x6000));
    }
    first->bus.memory.merge_bank_accesses(summary);
    CHECK(first->bus.memory.row_open(0x5000));
    first->bus.memory.write(0x7000, 4, 3);
    CHECK(first->bus.memory.row_open(0x7000));
    CHECK(!first->bus.memory.row_open(0x5000));
    CHECK(second->bus.memory.row_open(0x6000));
}
