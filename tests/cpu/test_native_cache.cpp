#include "cupid/cpu/native_cache.hpp"
#include "test.hpp"

#include <array>
#include <utility>

namespace {
using namespace cupid;

std::array<u32, 1> immediate(unsigned value) {
    return {0x24010000U | (value & 65535U)};
}
} // namespace

TEST(cpu_native_cache_keys_include_complete_instruction_bytes_and_length) {
    CpuNativeCache cache;
    CHECK(!cache.lookup({}));
    const std::array<u32, CpuNativeCode::maximum_instructions + 1U> oversized{};
    CHECK(!cache.lookup(oversized));
    CHECK_EQ(cache.size(), 0U);
    if (!CpuNativeCode::available()) {
        CHECK(!cache.lookup(immediate(1U)));
        CHECK_EQ(cache.size(), 0U);
        return;
    }
    const std::array first{0x24010001U, 0x24220002U, 0x0041182dU};
    const std::array identical = first;
    const auto code = cache.lookup(first);
    CHECK(code != nullptr);
    CHECK_EQ(cache.lookup(identical), code);
    CHECK_EQ(cache.size(), 1U);
    const auto shortened = cache.lookup(std::span(first).first(2U));
    CHECK(shortened != nullptr);
    CHECK(shortened != code);
    auto changed = first;
    changed[1] = 0x24220003U;
    const auto replaced = cache.lookup(changed);
    CHECK(replaced != nullptr);
    CHECK(replaced != code);
    CHECK_EQ(cache.size(), 3U);
    for (const auto& selected : {code, shortened, replaced}) {
        std::array<u64, 32> registers{};
        CHECK(selected->execute(registers));
        CHECK_EQ(registers[1], 1U);
        CHECK_EQ(registers[2], selected == replaced ? 4U : 3U);
        CHECK_EQ(registers[3], selected == shortened ? 0U : selected == replaced ? 5U : 4U);
    }
    const std::array unsupported{0x20010001U};
    CHECK(!cache.lookup(unsupported));
    CHECK_EQ(cache.size(), 4U);
    CHECK(!cache.lookup(unsupported));
    CHECK_EQ(cache.size(), 4U);
}

TEST(cpu_native_cache_bounds_lookup_owners_without_invalidating_retained_programs) {
    if (!CpuNativeCode::available())
        return;
    CpuNativeCache cache;
    auto retained = cache.lookup(immediate(0U));
    const std::weak_ptr<const CpuNativeCode> retained_weak = retained;
    std::weak_ptr<const CpuNativeCode> discarded;
    for (std::size_t index = 1; index < CpuNativeCache::capacity; ++index) {
        const auto code = cache.lookup(immediate(static_cast<unsigned>(index)));
        CHECK(code != nullptr);
        if (index == 1U)
            discarded = code;
        CHECK_EQ(cache.size(), index + 1U);
    }
    CHECK(!discarded.expired());
    CHECK(cache.lookup(immediate(static_cast<unsigned>(CpuNativeCache::capacity))) != nullptr);
    CHECK_EQ(cache.size(), 1U);
    CHECK(discarded.expired());
    CHECK(!retained_weak.expired());
    std::array<u64, 32> registers{};
    registers[1] = ~0ULL;
    CHECK(retained->execute(registers));
    CHECK_EQ(registers[1], 0U);
    retained.reset();
    CHECK(retained_weak.expired());
    const std::weak_ptr<const CpuNativeCode> remaining =
        cache.lookup(immediate(static_cast<unsigned>(CpuNativeCache::capacity)));
    cache.reset();
    CHECK_EQ(cache.size(), 0U);
    CHECK(remaining.expired());
}

TEST(cpu_native_cache_copies_keep_independent_owners_and_moves_transfer_lookup_storage) {
    if (!CpuNativeCode::available())
        return;
    CpuNativeCache original;
    const auto first = original.lookup(immediate(1U));
    CpuNativeCache copied(original);
    CHECK_EQ(copied.size(), 0U);
    const auto second = copied.lookup(immediate(1U));
    CHECK(second != nullptr);
    CHECK(second != first);
    CpuNativeCache assigned;
    auto previous = assigned.lookup(immediate(2U));
    const std::weak_ptr<const CpuNativeCode> previous_weak = previous;
    previous.reset();
    assigned = original;
    CHECK_EQ(assigned.size(), 0U);
    CHECK(previous_weak.expired());
    const auto self_owned = assigned.lookup(immediate(3U));
    const auto* same = &assigned;
    assigned = *same;
    CHECK_EQ(assigned.size(), 1U);
    CHECK_EQ(assigned.lookup(immediate(3U)), self_owned);
    CpuNativeCache moved(std::move(original));
    CHECK_EQ(original.size(), 0U);
    CHECK_EQ(moved.size(), 1U);
    CHECK_EQ(moved.lookup(immediate(1U)), first);
    assigned = std::move(moved);
    CHECK_EQ(moved.size(), 0U);
    CHECK_EQ(assigned.lookup(immediate(1U)), first);
}

TEST(cpu_native_cache_find_does_not_allocate_compile_or_evict_a_lookup_generation) {
    CpuNativeCache cache;
    CHECK(!cache.find({}));
    CHECK(!cache.find(immediate(1U)));
    const std::array<u32, CpuNativeCode::maximum_instructions + 1U> oversized{};
    CHECK(!cache.find(oversized));
    CHECK_EQ(cache.size(), 0U);
    if (!CpuNativeCode::available())
        return;
    const std::array first{0x24010001U, 0x24220002U, 0x0041182dU};
    auto changed = first;
    changed[1] = 0x24220003U;
    const auto retained = cache.lookup(first);
    CHECK(retained != nullptr);
    CHECK_EQ(cache.find(first), retained);
    CHECK(!cache.find(changed));
    CHECK(!cache.find(std::span(first).first(2U)));
    const std::array unsupported{0x20010001U};
    CHECK(!cache.lookup(unsupported));
    CHECK(!cache.find(unsupported));
    CHECK_EQ(cache.size(), 2U);
    for (unsigned index = 2; index < CpuNativeCache::capacity; ++index)
        CHECK(cache.lookup(immediate(index)) != nullptr);
    CHECK_EQ(cache.size(), CpuNativeCache::capacity);
    CHECK(!cache.find(immediate(static_cast<unsigned>(CpuNativeCache::capacity))));
    CHECK_EQ(cache.size(), CpuNativeCache::capacity);
    CHECK_EQ(cache.find(first), retained);
    cache.reset();
    CHECK(!cache.find(first));
    CHECK_EQ(cache.size(), 0U);
}
