#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <memory>

using namespace cupid;

namespace {
void access_group(Rdram& memory, unsigned group) {
    for (u32 bank = 0; bank < 8U; ++bank) {
        const u32 row = group == 1U && (bank & 1U) != 0U ? 5U : 3U;
        const u32 address = (bank << 20U) + (row << 11U);
        memory.advance_clock(group + 1U);
        if (group == 0U || (bank & 2U) != 0U)
            memory.write(address, 4U, 0x11223344U + bank + group);
        (void)memory.read(address, 4U);
    }
}
} // namespace

namespace {
void compare_nested(bool check_deferred) {
    auto serial = std::make_unique<System>();
    auto scoped = std::make_unique<System>();
    test::initialize_memory(*serial);
    test::initialize_memory(*scoped);
    const auto initial_status = scoped->bus.memory.bank_status();
    Rdram::BankAccessSummary outer, inner;
    {
        const Rdram::BankAccessScope outer_scope(scoped->bus.memory, outer);
        access_group(serial->bus.memory, 0U);
        access_group(scoped->bus.memory, 0U);
        {
            const Rdram::BankAccessScope inner_scope(scoped->bus.memory, inner);
            access_group(serial->bus.memory, 1U);
            access_group(scoped->bus.memory, 1U);
        }
        scoped->bus.memory.merge_bank_accesses(inner);
        if (check_deferred)
            CHECK_EQ(scoped->bus.memory.bank_status(), initial_status);
        access_group(serial->bus.memory, 2U);
        access_group(scoped->bus.memory, 2U);
    }
    if (check_deferred)
        CHECK_EQ(scoped->bus.memory.bank_status(), initial_status);
    scoped->bus.memory.merge_bank_accesses(outer);
    CHECK_EQ(scoped->bus.rdram, serial->bus.rdram);
    CHECK_EQ(scoped->bus.memory.bank_status(), serial->bus.memory.bank_status());
    CHECK_EQ(scoped->bus.memory.clock(), serial->bus.memory.clock());
    CHECK_EQ(scoped->bus.memory.errors(), serial->bus.memory.errors());
    for (u32 bank = 0; bank < 8U; ++bank) {
        CHECK_EQ(scoped->bus.memory.bank_access_clock(bank << 20U),
                 serial->bus.memory.bank_access_clock(bank << 20U));
        for (u32 row = 0; row < 512U; ++row) {
            const auto address = (bank << 20U) + (row << 11U);
            CHECK_EQ(scoped->bus.memory.row_open(address), serial->bus.memory.row_open(address));
        }
    }
}
} // namespace

TEST(rdram_nested_summaries_remain_deferred_and_preserve_serial_row_order) {
    compare_nested(true);
}

TEST(rdram_nested_summaries_preserve_final_dirty_state_after_inner_row_changes) {
    compare_nested(false);
}

TEST(rdram_nested_scopes_find_the_matching_memory_beneath_another_machine) {
    auto serial = std::make_unique<System>();
    auto scoped = std::make_unique<System>();
    auto other = std::make_unique<System>();
    for (auto* system : {serial.get(), scoped.get(), other.get()})
        test::initialize_memory(*system);
    Rdram::BankAccessSummary outer, inner, other_summary;
    {
        const Rdram::BankAccessScope outer_scope(scoped->bus.memory, outer);
        for (auto* system : {serial.get(), scoped.get()}) {
            system->bus.memory.advance_clock(1U);
            system->bus.memory.write(3U << 11U, 4U, 0x11223344U);
        }
        {
            const Rdram::BankAccessScope other_scope(other->bus.memory, other_summary);
            other->bus.memory.advance_clock(4U);
            other->bus.memory.write(2U << 11U, 4U, 0x55667788U);
            {
                const Rdram::BankAccessScope inner_scope(scoped->bus.memory, inner);
                for (auto* system : {serial.get(), scoped.get()}) {
                    system->bus.memory.advance_clock(2U);
                    (void)system->bus.memory.read(5U << 11U, 4U);
                }
            }
            scoped->bus.memory.merge_bank_accesses(inner);
            for (auto* system : {serial.get(), scoped.get()}) {
                system->bus.memory.advance_clock(4U);
                (void)system->bus.memory.read(7U << 11U, 4U);
            }
            CHECK_EQ(scoped->bus.memory.bank_status(), 0U);
            CHECK_EQ(other->bus.memory.bank_status(), 0U);
        }
        other->bus.memory.merge_bank_accesses(other_summary);
        for (auto* system : {serial.get(), scoped.get()}) {
            system->bus.memory.advance_clock(8U);
            (void)system->bus.memory.read(3U << 11U, 4U);
        }
    }
    scoped->bus.memory.merge_bank_accesses(outer);
    CHECK_EQ(scoped->bus.memory.bank_status(), 1U);
    CHECK_EQ(scoped->bus.memory.bank_status(), serial->bus.memory.bank_status());
    CHECK_EQ(scoped->bus.memory.bank_access_clock(0U), serial->bus.memory.bank_access_clock(0U));
    CHECK(scoped->bus.memory.row_open(3U << 11U));
    CHECK_EQ(scoped->bus.memory.clock(), 15U);
    CHECK_EQ(other->bus.memory.bank_status(), 0x101U);
    CHECK(other->bus.memory.row_open(2U << 11U));
    CHECK_EQ(other->bus.memory.bank_access_clock(0U), 4U);
}
