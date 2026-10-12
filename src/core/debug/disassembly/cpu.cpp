#include "core/debug/disassembly/fields.hpp"
#include <array>

namespace cupid::n64::disassembly {
namespace {

std::string floating(const Fields &f) {
  const auto format = f.rs();
  constexpr std::array<std::string_view, 8> transfer{"mfc1", "dmfc1", "cfc1", "",
                                                     "mtc1", "dmtc1", "ctc1", ""};
  if (format < 8)
    return operation(transfer[format], {reg(f.rt()), reg(f.rd(), (format & 2) ? 'c' : 'f')});
  if (format == 8) {
    constexpr std::array<std::string_view, 4> branches{"bc1f", "bc1t", "bc1fl", "bc1tl"};
    return operation(branches[f.rt() & 3], {f.target()});
  }
  const auto suffix = format == 16 ? "s" : format == 17 ? "d" : format == 20 ? "w" : "l";
  if (format != 16 && format != 17 && format != 20 && format != 21)
    return {};
  const auto d = reg(f.sa(), 'f'), s = reg(f.rd(), 'f'), t = reg(f.rt(), 'f');
  const auto fn = f.fn();
  if (fn == 32 || fn == 33 || fn == 36 || fn == 37) {
    const auto dest = fn == 32 ? "s" : fn == 33 ? "d" : fn == 36 ? "w" : "l";
    return operation(std::string("cvt.") + dest + "." + suffix, {d, s});
  }
  if (format >= 20)
    return {};
  if (fn < 16) {
    constexpr std::array<std::string_view, 16> names{
        "add",     "sub",     "mul",    "div",     "sqrt",    "abs",     "mov",    "neg",
        "round.l", "trunc.l", "ceil.l", "floor.l", "round.w", "trunc.w", "ceil.w", "floor.w"};
    const auto name = std::string(names[fn]) + "." + suffix;
    return fn < 4 ? operation(name, {d, s, t}) : operation(name, {d, s});
  }
  if (fn >= 48) {
    constexpr std::array<std::string_view, 16> predicates{"f",   "un",  "eq", "ueq",  "olt", "ult",
                                                          "ole", "ule", "sf", "ngle", "seq", "ngl",
                                                          "lt",  "nge", "le", "ngt"};
    return operation(std::string("c.") + std::string(predicates[fn - 48]) + "." + suffix, {s, t});
  }
  return {};
}

} // namespace

std::string cpu(const Fields &f) {
  if (f.op() == 17)
    return floating(f);
  if (f.op() == 16 || f.op() == 18) {
    if (f.rs() < 8) {
      constexpr std::array<std::string_view, 8> transfer{"mfc", "dmfc", "cfc", "",
                                                         "mtc", "dmtc", "ctc", ""};
      const auto name = transfer[f.rs()];
      if (name.empty())
        return {};
      return operation(std::string(name) + (f.op() == 16 ? "0" : "2"),
                       {reg(f.rt()), reg(f.rd(), 'c')});
    }
    if (f.op() == 16 && (f.rs() & 16)) {
      if (const auto extension = homebrew(DebugProcessor::Cpu, f.word); !extension.empty())
        return extension;
      switch (f.fn()) {
      case 1:
        return "tlbr";
      case 2:
        return "tlbwi";
      case 6:
        return "tlbwr";
      case 8:
        return "tlbp";
      case 24:
        return "eret";
      }
    }
    return {};
  }
  return scalar(f);
}

} // namespace cupid::n64::disassembly

namespace cupid::n64 {

std::string disassemble(DebugProcessor processor, std::uint64_t pc, std::uint32_t instruction) {
  const disassembly::Fields fields{pc, instruction, processor == DebugProcessor::Rsp};
  auto decoded = processor == DebugProcessor::Cpu   ? disassembly::cpu(fields)
                 : processor == DebugProcessor::Rsp ? disassembly::signal(fields)
                                                    : std::string();
  return decoded.empty() ? ".word " + disassembly::hex(instruction, 8) : decoded;
}

} // namespace cupid::n64
