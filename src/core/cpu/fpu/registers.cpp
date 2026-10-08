#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

bool Cpu::fpu_enabled() {
  if (control_[Status] & 0x20000000)
    return true;
  raise(Exception::CoprocessorUnusable, 1);
  return false;
}

bool Cpu::fpu_begin() {
  if (!fpu_enabled())
    return false;
  state_.fcr31 &= ~0x0003f000u;
  return true;
}

unsigned Cpu::fpu_source(unsigned index) const {
  return control_[Status] & 0x04000000 ? index : index & ~1u;
}

std::uint64_t Cpu::fpu_transfer(unsigned index, bool wide) const {
  const auto value = state_.fpr[fpu_source(index)];
  if (wide)
    return value;
  const auto shift = !(control_[Status] & 0x04000000) && (index & 1) ? 32 : 0;
  return (value >> shift) & 0xffffffff;
}

void Cpu::fpu_transfer(unsigned index, bool wide, std::uint64_t value) {
  auto &reg = state_.fpr[fpu_source(index)];
  if (wide)
    reg = value;
  else {
    const auto shift = !(control_[Status] & 0x04000000) && (index & 1) ? 32 : 0;
    reg = (reg & ~(0xffffffffull << shift)) | ((value & 0xffffffff) << shift);
  }
}

void Cpu::fpu_memory(unsigned operation, unsigned reg, std::uint64_t address) {
  if (!fpu_enabled())
    return;
  const bool wide = operation & 4;
  if (operation & 8)
    write(address, wide ? 8 : 4, fpu_transfer(reg, wide));
  else if (const auto value = read(address, wide ? 8 : 4))
    fpu_transfer(reg, wide, *value);
}

void Cpu::cop1(std::uint32_t instruction) {
  const auto format = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto source = (instruction >> 11) & 31;
  if (!fpu_enabled())
    return;
  switch (format) {
  case 0:
    state_.gpr[rt] = sign_word(static_cast<std::uint32_t>(fpu_transfer(source, false)));
    return;
  case 1:
    state_.gpr[rt] = fpu_transfer(source, true);
    return;
  case 2:
    state_.gpr[rt] = source == 0 ? 0x0a00 : source == 31 ? state_.fcr31 : 0;
    return;
  case 4:
    return fpu_transfer(source, false, state_.gpr[rt]);
  case 5:
    return fpu_transfer(source, true, state_.gpr[rt]);
  case 6:
    if (source == 31) {
      state_.fcr31 = static_cast<std::uint32_t>(state_.gpr[rt]) & 0x0183ffff;
      const auto cause = (state_.fcr31 >> 12) & 31;
      const auto enable = (state_.fcr31 >> 7) & 31;
      if ((cause & enable) || (state_.fcr31 & 0x20000))
        raise(Exception::FloatingPoint);
    }
    return;
  case 8:
    if (!fpu_begin())
      return;
    if (rt >= 4) {
      fpu_unimplemented();
      return;
    }
    return branch(bool(state_.fcr31 & 0x800000) == bool(rt & 1), rt & 2,
                  std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction)));
  default:
    break;
  }
  if (format == 16)
    return fpu_arithmetic<float>(instruction);
  if (format == 17)
    return fpu_arithmetic<double>(instruction);
  if (format == 20 || format == 21) {
    if (!fpu_begin())
      return;
    const auto dest = (instruction >> 6) & 31;
    switch (instruction & 63) {
    case 0x20:
      return fpu_convert<float>(dest, source, format);
    case 0x21:
      return fpu_convert<double>(dest, source, format);
    default:
      break;
    }
    fpu_unimplemented();
    return;
  }
  if (fpu_begin())
    fpu_unimplemented();
}

} // namespace cupid::n64
