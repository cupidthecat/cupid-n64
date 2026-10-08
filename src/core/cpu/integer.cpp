#include "core/cpu/cpu.hpp"
#include <limits>

namespace cupid::n64 {

void Cpu::add(unsigned dest, std::uint64_t a, std::uint64_t b, bool wide, bool trap) {
  if (wide && !require_doubleword())
    return;
  const auto result = a + b;
  const auto sign = wide ? 1ull << 63 : 1ull << 31;
  if (trap && (~(a ^ b) & (a ^ result) & sign))
    return raise(Exception::Overflow);
  state_.gpr[dest] = wide ? result : sign_word(static_cast<std::uint32_t>(result));
}

void Cpu::subtract(unsigned dest, std::uint64_t a, std::uint64_t b, bool wide, bool trap) {
  if (wide && !require_doubleword())
    return;
  const auto result = a - b;
  const auto sign = wide ? 1ull << 63 : 1ull << 31;
  if (trap && ((a ^ b) & (a ^ result) & sign))
    return raise(Exception::Overflow);
  state_.gpr[dest] = wide ? result : sign_word(static_cast<std::uint32_t>(result));
}

unsigned Cpu::multiply(std::uint64_t a, std::uint64_t b, bool wide, bool is_signed) {
  if (wide) {
    if (!require_doubleword())
      return 0;
    const auto a0 = a & 0xffffffff;
    const auto a1 = a >> 32;
    const auto b0 = b & 0xffffffff;
    const auto b1 = b >> 32;
    const auto p0 = a0 * b0;
    const auto p1 = a1 * b0 + (p0 >> 32);
    const auto p2 = a0 * b1 + (p1 & 0xffffffff);
    state_.lo = a * b;
    state_.hi = a1 * b1 + (p1 >> 32) + (p2 >> 32);
    if (is_signed) {
      if (a >> 63)
        state_.hi -= b;
      if (b >> 63)
        state_.hi -= a;
    }
    return 14;
  }
  const auto right =
      is_signed ? static_cast<std::uint64_t>(signed_value(b << 29) >> 29) : b & 0xffffffff;
  const auto result = (is_signed ? a : a & 0xffffffff) * right;
  state_.lo = sign_word(static_cast<std::uint32_t>(result));
  state_.hi = sign_word(static_cast<std::uint32_t>(result >> 32));
  return 8;
}

unsigned Cpu::divide(std::uint64_t a, std::uint64_t b, bool wide, bool is_signed) {
  if (wide && !require_doubleword())
    return 0;
  if (is_signed) {
    const auto numerator =
        wide ? signed_value(a) : signed_value(sign_word(static_cast<std::uint32_t>(a)));
    const auto denominator = signed_value(b);
    if (!denominator) {
      state_.lo = numerator < 0 ? 1 : ~0ull;
      state_.hi = static_cast<std::uint64_t>(numerator);
    } else if (numerator == std::numeric_limits<std::int64_t>::min() && denominator == -1) {
      state_.lo = a;
      state_.hi = 0;
    } else {
      state_.lo = static_cast<std::uint64_t>(numerator / denominator);
      state_.hi = static_cast<std::uint64_t>(numerator % denominator);
    }
  } else {
    const auto numerator = wide ? a : a & 0xffffffff;
    const auto denominator = wide ? b : b & 0xffffffff;
    state_.lo = denominator ? numerator / denominator : ~0ull;
    state_.hi = denominator ? numerator % denominator : numerator;
  }
  if (!wide) {
    state_.lo = sign_word(static_cast<std::uint32_t>(state_.lo));
    state_.hi = sign_word(static_cast<std::uint32_t>(state_.hi));
  }
  return wide ? 136 : 72;
}

void Cpu::decode(std::uint32_t instruction) {
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto immediate = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction));
  const auto signed_immediate = static_cast<std::uint64_t>(std::int64_t(immediate));
  const auto a = state_.gpr[rs];
  const auto b = state_.gpr[rt];
  switch (instruction >> 26) {
  case 0x00:
    return special(instruction);
  case 0x01:
    return regimm(instruction);
  case 0x02:
    return jump((pipeline_pc_ & 0xfffffffff0000000ull) | ((instruction & 0x03ffffffull) << 2));
  case 0x03:
    state_.gpr[31] = pipeline_pc_ + 4;
    return jump((pipeline_pc_ & 0xfffffffff0000000ull) | ((instruction & 0x03ffffffull) << 2));
  case 0x04:
    return branch(a == b, false, immediate);
  case 0x05:
    return branch(a != b, false, immediate);
  case 0x06:
    return branch(signed_value(a) <= 0, false, immediate);
  case 0x07:
    return branch(signed_value(a) > 0, false, immediate);
  case 0x08:
    return add(rt, a, signed_immediate, false, true);
  case 0x09:
    return add(rt, a, signed_immediate, false, false);
  case 0x0a:
    state_.gpr[rt] = signed_value(a) < immediate;
    return;
  case 0x0b:
    state_.gpr[rt] = a < signed_immediate;
    return;
  case 0x0c:
    state_.gpr[rt] = a & (instruction & 0xffff);
    return;
  case 0x0d:
    state_.gpr[rt] = a | (instruction & 0xffff);
    return;
  case 0x0e:
    state_.gpr[rt] = a ^ (instruction & 0xffff);
    return;
  case 0x0f:
    state_.gpr[rt] = sign_word(instruction << 16);
    return;
  case 0x10:
    return cop0(instruction);
  case 0x11:
    return cop1(instruction);
  case 0x12:
    return cop2(instruction);
  case 0x13:
    return raise(Exception::ReservedInstruction);
  case 0x14:
    return branch(a == b, true, immediate);
  case 0x15:
    return branch(a != b, true, immediate);
  case 0x16:
    return branch(signed_value(a) <= 0, true, immediate);
  case 0x17:
    return branch(signed_value(a) > 0, true, immediate);
  case 0x18:
    return add(rt, a, signed_immediate, true, true);
  case 0x19:
    return add(rt, a, signed_immediate, true, false);
  case 0x1c:
  case 0x1d:
  case 0x1e:
  case 0x1f:
    return raise(Exception::ReservedInstruction);
  default:
    return load_store(instruction);
  }
}

void Cpu::special(std::uint32_t instruction) {
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto rd = (instruction >> 11) & 31;
  const auto shift = (instruction >> 6) & 31;
  const auto a = state_.gpr[rs];
  const auto b = state_.gpr[rt];
  switch (instruction & 63) {
  case 0x00:
    state_.gpr[rd] = sign_word(static_cast<std::uint32_t>(b) << shift);
    return;
  case 0x02:
    state_.gpr[rd] = sign_word(static_cast<std::uint32_t>(b) >> shift);
    return;
  case 0x03:
    state_.gpr[rd] = sign_word(static_cast<std::uint32_t>(signed_value(b) >> shift));
    return;
  case 0x04:
    state_.gpr[rd] = sign_word(static_cast<std::uint32_t>(b) << (a & 31));
    return;
  case 0x06:
    state_.gpr[rd] = sign_word(static_cast<std::uint32_t>(b) >> (a & 31));
    return;
  case 0x07:
    state_.gpr[rd] = sign_word(static_cast<std::uint32_t>(signed_value(b) >> (a & 31)));
    return;
  case 0x08:
    return jump(a);
  case 0x09:
    state_.gpr[rd] = pipeline_pc_ + 4;
    return jump(a);
  case 0x0c:
    return raise(Exception::Syscall);
  case 0x0d:
    return raise(Exception::Breakpoint);
  case 0x0f:
    return;
  case 0x10:
    state_.gpr[rd] = state_.hi;
    return;
  case 0x11:
    state_.hi = a;
    return;
  case 0x12:
    state_.gpr[rd] = state_.lo;
    return;
  case 0x13:
    state_.lo = a;
    return;
  case 0x14:
    if (require_doubleword())
      state_.gpr[rd] = b << (a & 63);
    return;
  case 0x16:
    if (require_doubleword())
      state_.gpr[rd] = b >> (a & 63);
    return;
  case 0x17:
    if (require_doubleword())
      state_.gpr[rd] = static_cast<std::uint64_t>(signed_value(b) >> (a & 63));
    return;
  case 0x18:
    return advance_clocks(multiply(a, b, false, true));
  case 0x19:
    return advance_clocks(multiply(a, b, false, false));
  case 0x1a:
    return advance_clocks(divide(a, b, false, true));
  case 0x1b:
    return advance_clocks(divide(a, b, false, false));
  case 0x1c:
    return advance_clocks(multiply(a, b, true, true));
  case 0x1d:
    return advance_clocks(multiply(a, b, true, false));
  case 0x1e:
    return advance_clocks(divide(a, b, true, true));
  case 0x1f:
    return advance_clocks(divide(a, b, true, false));
  case 0x20:
    return add(rd, a, b, false, true);
  case 0x21:
    return add(rd, a, b, false, false);
  case 0x22:
    return subtract(rd, a, b, false, true);
  case 0x23:
    return subtract(rd, a, b, false, false);
  case 0x24:
    state_.gpr[rd] = a & b;
    return;
  case 0x25:
    state_.gpr[rd] = a | b;
    return;
  case 0x26:
    state_.gpr[rd] = a ^ b;
    return;
  case 0x27:
    state_.gpr[rd] = ~(a | b);
    return;
  case 0x2a:
    state_.gpr[rd] = signed_value(a) < signed_value(b);
    return;
  case 0x2b:
    state_.gpr[rd] = a < b;
    return;
  case 0x2c:
    return add(rd, a, b, true, true);
  case 0x2d:
    return add(rd, a, b, true, false);
  case 0x2e:
    return subtract(rd, a, b, true, true);
  case 0x2f:
    return subtract(rd, a, b, true, false);
  case 0x30:
    if (signed_value(a) >= signed_value(b))
      raise(Exception::Trap);
    return;
  case 0x31:
    if (a >= b)
      raise(Exception::Trap);
    return;
  case 0x32:
    if (signed_value(a) < signed_value(b))
      raise(Exception::Trap);
    return;
  case 0x33:
    if (a < b)
      raise(Exception::Trap);
    return;
  case 0x34:
    if (a == b)
      raise(Exception::Trap);
    return;
  case 0x36:
    if (a != b)
      raise(Exception::Trap);
    return;
  case 0x38:
    if (require_doubleword())
      state_.gpr[rd] = b << shift;
    return;
  case 0x3a:
    if (require_doubleword())
      state_.gpr[rd] = b >> shift;
    return;
  case 0x3b:
    if (require_doubleword())
      state_.gpr[rd] = static_cast<std::uint64_t>(signed_value(b) >> shift);
    return;
  case 0x3c:
    if (require_doubleword())
      state_.gpr[rd] = b << (shift + 32);
    return;
  case 0x3e:
    if (require_doubleword())
      state_.gpr[rd] = b >> (shift + 32);
    return;
  case 0x3f:
    if (require_doubleword())
      state_.gpr[rd] = static_cast<std::uint64_t>(signed_value(b) >> (shift + 32));
    return;
  default:
    return raise(Exception::ReservedInstruction);
  }
}

void Cpu::regimm(std::uint32_t instruction) {
  const auto rs = (instruction >> 21) & 31;
  const auto function = (instruction >> 16) & 31;
  const auto immediate = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction));
  const auto a = state_.gpr[rs];
  const auto b = static_cast<std::uint64_t>(std::int64_t(immediate));
  switch (function) {
  case 0x00:
    return branch(signed_value(a) < 0, false, immediate);
  case 0x01:
    return branch(signed_value(a) >= 0, false, immediate);
  case 0x02:
    return branch(signed_value(a) < 0, true, immediate);
  case 0x03:
    return branch(signed_value(a) >= 0, true, immediate);
  case 0x08:
    if (signed_value(a) >= immediate)
      raise(Exception::Trap);
    return;
  case 0x09:
    if (a >= b)
      raise(Exception::Trap);
    return;
  case 0x0a:
    if (signed_value(a) < immediate)
      raise(Exception::Trap);
    return;
  case 0x0b:
    if (a < b)
      raise(Exception::Trap);
    return;
  case 0x0c:
    if (a == b)
      raise(Exception::Trap);
    return;
  case 0x0e:
    if (a != b)
      raise(Exception::Trap);
    return;
  case 0x10:
  case 0x12:
    state_.gpr[31] = sign_word(static_cast<std::uint32_t>(pipeline_pc_ + 4));
    return branch(signed_value(state_.gpr[rs]) < 0, function == 0x12, immediate);
  case 0x11:
    branch(signed_value(a) >= 0, false, immediate);
    state_.gpr[31] = sign_word(static_cast<std::uint32_t>(pipeline_pc_ + 4));
    return;
  case 0x13:
    state_.gpr[31] = sign_word(static_cast<std::uint32_t>(pipeline_pc_ + 4));
    return branch(signed_value(state_.gpr[rs]) >= 0, true, immediate);
  default:
    return raise(Exception::ReservedInstruction);
  }
}

} // namespace cupid::n64
