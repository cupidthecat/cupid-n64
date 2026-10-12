#include "core/debug/disassembly/fields.hpp"
#include <array>

namespace cupid::n64::disassembly {

std::string scalar(const Fields &f) {
  const auto s = reg(f.rs()), t = reg(f.rt()), d = reg(f.rd());
  if (!f.word)
    return "nop";
  if (f.op() == 0) {
    constexpr std::array<std::string_view, 64> names{
        "sll",    "",      "srl",     "sra",   "sllv", "",      "srlv", "srav",  "jr",    "jalr",
        "",       "",      "syscall", "break", "",     "sync",  "mfhi", "mthi",  "mflo",  "mtlo",
        "dsllv",  "",      "dsrlv",   "dsrav", "mult", "multu", "div",  "divu",  "dmult", "dmultu",
        "ddiv",   "ddivu", "add",     "addu",  "sub",  "subu",  "and",  "or",    "xor",   "nor",
        "",       "",      "slt",     "sltu",  "dadd", "daddu", "dsub", "dsubu", "tge",   "tgeu",
        "tlt",    "tltu",  "teq",     "",      "tne",  "",      "dsll", "",      "dsrl",  "dsra",
        "dsll32", "",      "dsrl32",  "dsra32"};
    const auto fn = f.fn();
    const bool shared = fn <= 9 || fn == 13 || (fn >= 32 && fn <= 39) || fn == 42 || fn == 43;
    if (f.rsp && !shared)
      return {};
    const auto name = names[fn];
    if (fn == 0 || fn == 2 || fn == 3 || fn >= 56)
      return operation(name, {d, t, std::to_string(f.sa())});
    if (fn == 4 || fn == 6 || fn == 7 || fn == 20 || fn == 22 || fn == 23)
      return operation(name, {d, t, s});
    if (fn == 8 || fn == 17 || fn == 19)
      return operation(name, {s});
    if (fn == 9)
      return operation(name, {d, s});
    if (fn == 12 || fn == 13)
      return operation(name, {hex((f.word >> 6) & 0xfffff)});
    if (fn == 15)
      return operation(name, {std::to_string(f.sa())});
    if (fn == 16 || fn == 18)
      return operation(name, {d});
    if ((fn >= 24 && fn <= 31) || (fn >= 48 && fn <= 54))
      return operation(name, {s, t});
    return operation(name, {d, s, t});
  }
  if (f.op() == 1) {
    constexpr std::array<std::string_view, 32> names{
        "bltz", "bgez",  "bltzl", "bgezl", "",     "", "",       "",       "tgei",    "tgeiu",
        "tlti", "tltiu", "teqi",  "",      "tnei", "", "bltzal", "bgezal", "bltzall", "bgezall"};
    if (f.rsp && f.rt() != 0 && f.rt() != 1 && f.rt() != 16 && f.rt() != 17)
      return {};
    return operation(names[f.rt()],
                     {s, f.rt() >= 8 && f.rt() < 16 ? std::to_string(f.immediate()) : f.target()});
  }
  if (f.op() == 2 || f.op() == 3)
    return operation(f.op() == 2 ? "j" : "jal", {f.target(true)});
  if ((f.op() >= 4 && f.op() <= 7) || (!f.rsp && f.op() >= 20 && f.op() <= 23)) {
    constexpr std::array<std::string_view, 8> names{"beq",  "bne",  "blez",  "bgtz",
                                                    "beql", "bnel", "blezl", "bgtzl"};
    const auto index = (f.op() & 3) + (f.op() >= 20 ? 4 : 0);
    return (f.op() & 3) <= 1 ? operation(names[index], {s, t, f.target()})
                             : operation(names[index], {s, f.target()});
  }
  if (f.op() >= 8 && f.op() <= 15) {
    constexpr std::array<std::string_view, 8> names{"addi", "addiu", "slti", "sltiu",
                                                    "andi", "ori",   "xori", "lui"};
    const auto immediate = f.op() >= 12 ? hex(f.word & 0xffff) : std::to_string(f.immediate());
    return f.op() == 15 ? operation(names[7], {t, immediate})
                        : operation(names[f.op() - 8], {t, s, immediate});
  }
  if (!f.rsp && (f.op() == 24 || f.op() == 25))
    return operation(f.op() == 24 ? "daddi" : "daddiu", {t, s, std::to_string(f.immediate())});
  constexpr std::array<std::string_view, 32> memory_names{
      "lb",   "lh",  "lwl", "lw",   "lbu",   "lhu", "lwr",  "lwu",  "sb",   "sh",  "swl",
      "sw",   "sdl", "sdr", "swr",  "cache", "ll",  "lwc1", "lwc2", "",     "lld", "ldc1",
      "ldc2", "ld",  "sc",  "swc1", "swc2",  "",    "scd",  "sdc1", "sdc2", "sd"};
  if (!f.rsp && (f.op() == 26 || f.op() == 27))
    return operation(f.op() == 26 ? "ldl" : "ldr", {t, f.memory(f.immediate())});
  if (f.op() < 32)
    return {};
  const auto op = f.op();
  if (f.rsp && op != 32 && op != 33 && op != 35 && op != 36 && op != 37 && op != 39 && op != 40 &&
      op != 41 && op != 43)
    return {};
  const auto operand = op == 47                                       ? hex(f.rt())
                       : op == 49 || op == 53 || op == 57 || op == 61 ? reg(f.rt(), 'f')
                       : op == 50 || op == 54 || op == 58 || op == 62 ? reg(f.rt(), 'c')
                                                                      : t;
  return operation(memory_names[op - 32], {operand, f.memory(f.immediate())});
}

} // namespace cupid::n64::disassembly
