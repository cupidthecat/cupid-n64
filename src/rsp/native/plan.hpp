#pragma once

#include "cupid/rsp/native.hpp"

#include <array>

#if defined(CUPID_RSP_NATIVE)
namespace cupid::rsp_native {

struct VectorPlan {
    std::array<bool, 16> destination_live{};
    bool cache_accumulator{};
};

[[nodiscard]] VectorPlan plan_vectors(std::span<const RspNativeInstruction> instructions);

} // namespace cupid::rsp_native
#endif
