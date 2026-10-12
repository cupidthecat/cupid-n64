#pragma once

#include "core/debug/trace.hpp"
#include <array>
#include <string_view>

namespace cupid::n64 {

struct HomebrewCommand {
  unsigned function = 0, rd = 0, rt = 0, code = 0;
  static HomebrewCommand decode(std::uint32_t word) {
    return {word & 63, (word >> 20) & 31, (word >> 15) & 31, (word >> 6) & 511};
  }
  std::string_view name(DebugProcessor processor) const {
    switch (function) {
    case 0x20:
      return "xdetect";
    case 0x23:
      return processor == DebugProcessor::Rsp ? "xtracestart" : "";
    case 0x24:
      return processor == DebugProcessor::Rsp ? "xtracestop" : "";
    case 0x25:
      return "xlog";
    case 0x26:
      return processor == DebugProcessor::Rsp ? "xlogregs" : "";
    case 0x27:
      return "xhexdump";
    case 0x28:
      return "xprof";
    case 0x29:
      return "xprofread";
    case 0x2a:
      return "xexception";
    case 0x2c:
      return "xioctl";
    default:
      return "";
    }
  }
  static std::uint32_t capabilities(DebugProcessor processor) {
    if (processor != DebugProcessor::Cpu && processor != DebugProcessor::Rsp)
      return 0;
    std::uint32_t result = 0;
    for (unsigned number = 32; number < 64; ++number)
      if (!HomebrewCommand{number}.name(processor).empty())
        result |= 1u << (number - 32);
    return result;
  }
};

} // namespace cupid::n64
