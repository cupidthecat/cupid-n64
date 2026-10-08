#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

void Cpu::cop2_invalid() {
  raise(control_[Status] & 0x40000000 ? Exception::ReservedInstruction
                                      : Exception::CoprocessorUnusable,
        2);
}

void Cpu::cop2(std::uint32_t instruction) {
  if (!(control_[Status] & 0x40000000))
    return cop2_invalid();
  const auto rt = (instruction >> 16) & 31;
  switch ((instruction >> 21) & 31) {
  case 0:
  case 2:
    state_.gpr[rt] = sign_word(static_cast<std::uint32_t>(cop2_latch_));
    return;
  case 1:
    state_.gpr[rt] = cop2_latch_;
    return;
  case 4:
  case 5:
  case 6:
    cop2_latch_ = state_.gpr[rt];
    return;
  default:
    return cop2_invalid();
  }
}

} // namespace cupid::n64
