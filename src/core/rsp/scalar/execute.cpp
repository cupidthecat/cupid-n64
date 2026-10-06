#include "core/rsp/rsp.hpp"
#include <bit>

namespace cupid::n64 {

std::uint32_t Rsp::read_unaligned(std::uint32_t address, unsigned bytes) const {
  std::uint32_t value = 0;
  for (unsigned n = 0; n < bytes; ++n)
    value = (value << 8) | memory_[(address + n) & 0xfff];
  return value;
}

void Rsp::write_unaligned(std::uint32_t address, unsigned bytes, std::uint32_t value) {
  for (unsigned n = 0; n < bytes; ++n)
    memory_[(address + n) & 0xfff] = static_cast<std::uint8_t>(value >> ((bytes - n - 1) * 8));
}

void Rsp::take_branch(std::uint32_t address) {
  next_pc_ = address & 0xfff;
  next_delay_slot_ = true;
}

void Rsp::scalar_special(std::uint32_t instruction) {
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto dest = (instruction >> 11) & 31;
  const auto shift = (instruction >> 6) & 31;
  const auto a = state_.gpr[source];
  const auto b = state_.gpr[target];
  auto &result = state_.gpr[dest];
  switch (instruction & 63) {
  case 0:
    result = b << shift;
    break;
  case 2:
    result = b >> shift;
    break;
  case 3:
    result = static_cast<std::uint32_t>(std::bit_cast<std::int32_t>(b) >> shift);
    break;
  case 4:
    result = b << (a & 31);
    break;
  case 6:
    result = b >> (a & 31);
    break;
  case 7:
    result = static_cast<std::uint32_t>(std::bit_cast<std::int32_t>(b) >> (a & 31));
    break;
  case 8:
    take_branch(a);
    break;
  case 9:
    take_branch(a);
    result = (pc_ + 8) & 0xfff;
    break;
  case 13:
    status_.halted = status_.broken = true;
    if (status_.interrupt_on_break)
      interrupts_.raise(Interrupt::Signal);
    break;
  case 32:
  case 33:
    result = a + b;
    break;
  case 34:
  case 35:
    result = a - b;
    break;
  case 36:
    result = a & b;
    break;
  case 37:
    result = a | b;
    break;
  case 38:
    result = a ^ b;
    break;
  case 39:
    result = ~(a | b);
    break;
  case 42:
    result = std::bit_cast<std::int32_t>(a) < std::bit_cast<std::int32_t>(b);
    break;
  case 43:
    result = a < b;
    break;
  default:
    result = a >> (a & 31);
    break;
  }
}

void Rsp::decode(std::uint32_t instruction) {
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto immediate = static_cast<std::int16_t>(instruction);
  const auto a = state_.gpr[source];
  const auto address = a + static_cast<std::uint32_t>(immediate);
  const auto branch = [&](bool taken) {
    if (taken)
      take_branch(pc_ + 4 + static_cast<std::uint32_t>(immediate) * 4);
  };
  auto &result = state_.gpr[target];
  switch (instruction >> 26) {
  case 0:
    scalar_special(instruction);
    break;
  case 1:
    if (target == 0 || target == 16)
      branch(std::bit_cast<std::int32_t>(a) < 0);
    if (target == 1 || target == 17)
      branch(std::bit_cast<std::int32_t>(a) >= 0);
    if (target == 16 || target == 17)
      state_.gpr[31] = (pc_ + 8) & 0xfff;
    break;
  case 2:
    take_branch(instruction * 4);
    break;
  case 3:
    state_.gpr[31] = (pc_ + 8) & 0xfff;
    take_branch(instruction * 4);
    break;
  case 4:
    branch(a == result);
    break;
  case 5:
    branch(a != result);
    break;
  case 6:
    branch(std::bit_cast<std::int32_t>(a) <= 0);
    break;
  case 7:
    branch(std::bit_cast<std::int32_t>(a) > 0);
    break;
  case 8:
  case 9:
    result = address;
    break;
  case 10:
    result = std::bit_cast<std::int32_t>(a) < immediate;
    break;
  case 11:
    result = a < static_cast<std::uint32_t>(immediate);
    break;
  case 12:
    result = a & (instruction & 0xffff);
    break;
  case 13:
    result = a | (instruction & 0xffff);
    break;
  case 14:
    result = a ^ (instruction & 0xffff);
    break;
  case 15:
    result = instruction << 16;
    break;
  case 16: {
    const auto reg = (instruction >> 11) & 15;
    if (source == 0) {
      const auto value = reg < 8         ? read_io(reg * 4)
                         : display_read_ ? display_read_((reg & 7) * 4)
                                         : 0;
      if (target)
        result = value;
    }
    if (source == 4) {
      if (reg < 8)
        write_io(reg * 4, result);
      else if (display_write_)
        display_write_((reg & 7) * 4, result);
    }
    break;
  }
  case 18:
    if (source < 16)
      vector_transfer(instruction);
    else
      vector_execute(instruction);
    break;
  case 32:
    result = static_cast<std::uint32_t>(static_cast<std::int8_t>(read_unaligned(address, 1)));
    break;
  case 33:
    result = static_cast<std::uint32_t>(static_cast<std::int16_t>(read_unaligned(address, 2)));
    break;
  case 35:
  case 39:
    result = read_unaligned(address, 4);
    break;
  case 36:
    result = read_unaligned(address, 1);
    break;
  case 37:
    result = read_unaligned(address, 2);
    break;
  case 40:
    write_unaligned(address, 1, result);
    break;
  case 41:
    write_unaligned(address, 2, result);
    break;
  case 43:
    write_unaligned(address, 4, result);
    break;
  case 50:
  case 58:
    vector_memory(instruction);
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
