#include "core/debug/disassembly/fields.hpp"
#include <array>

namespace cupid::n64::disassembly {

std::string signal(const Fields &f) {
  if (f.op() == 16) {
    if (f.rs() >= 16)
      return homebrew(DebugProcessor::Rsp, f.word);
    constexpr std::array<std::string_view, 16> registers{
        "SP_MEM_ADDR", "SP_DRAM_ADDR", "SP_RD_LEN",    "SP_WR_LEN", "SP_STATUS",   "SP_DMA_FULL",
        "SP_DMA_BUSY", "SP_SEMAPHORE", "DPC_START",    "DPC_END",   "DPC_CURRENT", "DPC_STATUS",
        "DPC_CLOCK",   "DPC_BUFBUSY",  "DPC_PIPEBUSY", "DPC_TMEM"};
    return f.rs() == 0 || f.rs() == 4
               ? operation(f.rs() == 0 ? "mfc0" : "mtc0",
                           {reg(f.rt()), std::string(registers[f.rd() & 15])})
               : std::string();
  }
  if (f.op() == 50 || f.op() == 58) {
    const bool store = f.op() == 58;
    constexpr std::array<std::string_view, 12> names{"bv", "sv", "lv", "dv", "qv", "rv",
                                                     "pv", "uv", "hv", "fv", "wv", "tv"};
    constexpr std::array<unsigned, 12> scale{1, 2, 4, 8, 16, 16, 8, 8, 16, 16, 16, 16};
    const auto type = f.rd();
    if (type >= names.size() || (!store && type == 10))
      return {};
    const auto displacement = static_cast<int>(f.word & 127) - (f.word & 64 ? 128 : 0);
    const auto name = std::string(store ? "s" : "l") + std::string(names[type]);
    const auto vector = reg(f.rt(), 'v') + "[" + std::to_string((f.word >> 7) & 15) + "]";
    return operation(name, {vector, f.memory(displacement * static_cast<int>(scale[type]))});
  }
  if (f.op() != 18)
    return scalar(f);
  if (f.rs() < 16) {
    constexpr std::array<std::string_view, 8> names{"mfc2", "", "cfc2", "", "mtc2", "", "ctc2", ""};
    if (f.rs() >= names.size())
      return {};
    const auto element = (f.word >> 7) & 15;
    const auto operand =
        (f.rs() & 2) ? reg(f.rd(), 'c') : reg(f.rd(), 'v') + "[" + std::to_string(element) + "]";
    return operation(names[f.rs()], {reg(f.rt()), operand});
  }
  constexpr std::array<std::string_view, 64> names{
      "vmulf", "vmulu", "vrndp", "vmulq", "vmudl", "vmudm", "vmudn", "vmudh", "vmacf", "vmacu",
      "vrndn", "vmacq", "vmadl", "vmadm", "vmadn", "vmadh", "vadd",  "vsub",  "",      "vabs",
      "vaddc", "vsubc", "",      "",      "",      "",      "",      "",      "",      "vsar",
      "",      "",      "vlt",   "veq",   "vne",   "vge",   "vcl",   "vch",   "vcr",   "vmrg",
      "vand",  "vnand", "vor",   "vnor",  "vxor",  "vnxor", "",      "",      "vrcp",  "vrcpl",
      "vrcph", "vmov",  "vrsq",  "vrsql", "vrsqh", "vnop"};
  const auto fn = f.fn();
  const auto d = reg(f.sa(), 'v'), s = reg(f.rd(), 'v');
  const auto element = f.rs() & 15;
  const auto t = reg(f.rt(), 'v') + "[" + std::to_string(element) + "]";
  if (fn == 55)
    return "vnop";
  if (fn == 11)
    return operation(names[fn], {d});
  if (fn == 29)
    return operation(names[fn], {d, std::to_string(element)});
  if (fn >= 48 && fn <= 54)
    return operation(names[fn], {d + "[" + std::to_string(f.rd() & 7) + "]", t});
  return operation(names[fn], {d, s, t});
}

} // namespace cupid::n64::disassembly
