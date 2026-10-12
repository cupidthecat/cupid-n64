#pragma once

#include "../fixture.hpp"
#include "core/debug/inspection.hpp"
#include "core/state/core_state.hpp"
#include "desktop/debug/format.hpp"
#include <memory>

namespace tracing_test {

using namespace cupid::n64;
using test::equal;
constexpr auto all_traces = (trace_bit(TraceKind::Tlb) << 1) - 1;

using debug_test::code;
using debug_test::machine;
using debug_test::raw_word;

void instruction_tests();
void execution_tests();

} // namespace tracing_test
