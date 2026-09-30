#pragma once

#include "cupid/types.hpp"

#include <memory>
#include <span>

namespace cupid {

struct CpuNativeState {
    u64* registers{};
    void* data_cache{};
};

class CpuNativeCode {
  public:
    static constexpr unsigned maximum_instructions = 7;

    ~CpuNativeCode();
    CpuNativeCode(const CpuNativeCode&) = delete;
    CpuNativeCode& operator=(const CpuNativeCode&) = delete;

    [[nodiscard]] static bool available();
    [[nodiscard]] static bool supports(u32 instruction);
    [[nodiscard]] static std::shared_ptr<const CpuNativeCode> compile(std::span<const u32> instructions);
    [[nodiscard]] bool execute(CpuNativeState& state) const;
    [[nodiscard]] bool execute(std::span<u64, 32> registers) const;

  private:
    explicit CpuNativeCode(void* code) : code_(code) {}
    void* code_{};
};

} // namespace cupid
