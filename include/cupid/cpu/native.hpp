#pragma once

#include "cupid/types.hpp"

#include <array>
#include <memory>
#include <span>

namespace cupid {

struct CpuNativeState {
    struct Store {
        void* line;
        u64 value;
        u64 previous;
        unsigned offset;
        unsigned width;
        bool previous_dirty;
    };

    CpuNativeState(u64* selected_registers, void* selected_cache)
        : registers(selected_registers), data_cache(selected_cache) {}

    u64* registers{};
    void* data_cache{};
    unsigned store_count{};
    u32 branch_taken{};
    u64 branch_target{}, branch_link{};
    std::array<Store, 7> stores;

    void commit_stores();
    void rollback_stores();
};

class CpuNativeCode {
  public:
    static constexpr unsigned maximum_instructions = 7;

    ~CpuNativeCode();
    CpuNativeCode(const CpuNativeCode&) = delete;
    CpuNativeCode& operator=(const CpuNativeCode&) = delete;

    [[nodiscard]] static bool available();
    [[nodiscard]] static bool supports(u32 instruction);
    [[nodiscard]] static bool terminal_branch(u32 instruction);
    [[nodiscard]] static std::shared_ptr<const CpuNativeCode> compile(std::span<const u32> instructions);
    [[nodiscard]] bool execute(CpuNativeState& state) const;
    [[nodiscard]] bool execute(std::span<u64, 32> registers) const;

  private:
    explicit CpuNativeCode(void* code, bool links = false) : code_(code), links_(links) {}
    void* code_{};
    bool links_{};
};

} // namespace cupid
