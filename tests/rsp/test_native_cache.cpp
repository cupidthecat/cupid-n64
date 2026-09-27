#include "cupid/rsp/native.hpp"
#include "test.hpp"

#include <array>
#include <thread>

using namespace cupid;

namespace {

std::array<RspNativeInstruction, 2> program(u32 value) {
    using Op = RspPipeline::Operation;
    return {{{0x24210000U | (value & 0xffffU), Op::Addiu}, {0x00221826U, Op::Xor}}};
}

RspNativeCache::Lookup warm(RspNativeCache& cache, std::span<const RspNativeInstruction> code) {
    RspNativeCache::Lookup result;
    for (unsigned visit = 0; visit < 5; ++visit)
        result = cache.lookup(code);
    return result;
}

} // namespace

TEST(rsp_native_cache_reuses_returning_microcode_with_exact_interior_words) {
    RspNativeCache cache;
    const auto first_code = program(7);
    auto second_code = first_code;
    second_code[1].word = 0x00222026U;
    RspNativeCache::Lookup first, second;
    for (unsigned visit = 0; visit < 5; ++visit) {
        first = cache.lookup(first_code);
        second = cache.lookup(second_code);
    }
    CHECK(first.complete && second.complete);
    CHECK_EQ(static_cast<bool>(first.code), RspNativeCode::available());
    CHECK_EQ(static_cast<bool>(second.code), RspNativeCode::available());
    if (!first.code)
        return;
    CHECK(first.code != second.code);
    CHECK(cache.lookup(first_code).code == first.code);
    CHECK(cache.lookup(second_code).code == second.code);
    const auto shorter = warm(cache, std::span(first_code).first(1));
    CHECK(shorter.code && shorter.code != first.code);
}

TEST(rsp_native_cache_bounds_lookup_storage_without_invalidating_live_code) {
    RspNativeCache cache;
    const auto first_code = program(7);
    auto retained = warm(cache, first_code).code;
    if (!RspNativeCode::available()) {
        CHECK(!retained);
        CHECK_EQ(cache.size(), 0U);
        return;
    }
    CHECK(retained);
    for (std::size_t index = 0; index <= RspNativeCache::capacity; ++index) {
        (void)cache.lookup(program(static_cast<u32>(index + 16U)));
        CHECK(cache.size() <= RspNativeCache::capacity);
    }
    cache.reset();
    CHECK_EQ(cache.size(), 0U);
    std::array<u32, 32> registers{};
    registers[1] = 11;
    registers[2] = 0xff;
    RspNativeState state{nullptr, registers.data(), nullptr};
    retained->execute(state);
    CHECK_EQ(registers[1], 18U);
    CHECK_EQ(registers[3], 0xedU);
}

TEST(rsp_native_cache_copies_keep_compilation_state_independent) {
    RspNativeCache first;
    const auto code = program(7);
    auto retained = warm(first, code).code;
    RspNativeCache second(first);
    CHECK_EQ(second.size(), 0U);
    if (!retained)
        return;
    auto first_lookup = second.lookup(code);
    CHECK(!first_lookup.complete && !first_lookup.code);
    CHECK(first.lookup(code).code == retained);
    second = first;
    CHECK_EQ(second.size(), 0U);
    first.reset();
    std::array<u32, 32> registers{};
    RspNativeState state{nullptr, registers.data(), nullptr};
    retained->execute(state);
    CHECK_EQ(registers[1], 7U);
}

TEST(rsp_native_code_can_run_on_independent_threads_with_independent_storage) {
    const auto code = RspNativeCode::compile(program(1));
    if (!code) {
        CHECK(!RspNativeCode::available());
        return;
    }
    std::array<std::array<u32, 32>, 2> registers{};
    registers[1][1] = 700;
    std::array<std::thread, 2> threads;
    for (unsigned index = 0; index < threads.size(); ++index)
        threads[index] = std::thread([&, index] {
            RspNativeState state{nullptr, registers[index].data(), nullptr};
            for (unsigned iteration = 0; iteration < 1000; ++iteration)
                code->execute(state);
        });
    for (auto& thread : threads)
        thread.join();
    CHECK_EQ(registers[0][1], 1000U);
    CHECK_EQ(registers[1][1], 1700U);
}
