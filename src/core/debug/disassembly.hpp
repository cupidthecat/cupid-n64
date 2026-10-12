#pragma once

#include "core/debug/trace.hpp"
#include <string>

namespace cupid::n64 {

std::string disassemble(DebugProcessor processor, std::uint64_t pc, std::uint32_t instruction);

} // namespace cupid::n64
