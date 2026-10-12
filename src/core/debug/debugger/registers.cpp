#include "core/debug/debugger.hpp"
#include "core/system/console.hpp"

namespace cupid::n64 {

std::optional<std::uint64_t> Debugger::read_register(unsigned index) const {
  if (!attached_ || !halted_ || index >= register_count)
    return {};
  const auto &cpu = console_.cpu();
  const auto &state = cpu.state_;
  if (index < 32)
    return index ? state.gpr[index] : 0;
  switch (index) {
  case 32:
    return cpu.control_[Status];
  case 33:
    return state.lo;
  case 34:
    return state.hi;
  case 35:
    return cpu.control_[BadVAddr];
  case 36:
    return cpu.control_[Cause];
  case 37:
    return state.pc;
  case 70:
    return state.fcr31;
  default:
    return state.fpr[index - 38];
  }
}

bool Debugger::write_register(unsigned index, std::uint64_t value) {
  if (!attached_ || !halted_ || index >= register_count)
    return false;
  auto &cpu = console_.cpu();
  auto &state = cpu.state_;
  if (index < 32) {
    if (index)
      state.gpr[index] = value;
    return true;
  }
  switch (index) {
  case 32:
  case 35:
  case 36:
  case 70:
    return true;
  case 33:
    state.lo = value;
    break;
  case 34:
    state.hi = value;
    break;
  case 37:
    if (value & 3)
      return false;
    cpu.set_pc(canonical(value));
    break;
  default:
    state.fpr[index - 38] = value;
    break;
  }
  return true;
}

} // namespace cupid::n64
