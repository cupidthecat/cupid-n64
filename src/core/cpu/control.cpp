#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

unsigned Cpu::random_index() {
  entropy_ ^= entropy_ << 13;
  entropy_ ^= entropy_ >> 7;
  entropy_ ^= entropy_ << 17;
  const auto wired = static_cast<unsigned>(control_[Wired]);
  return wired > 31 ? static_cast<unsigned>(entropy_ & 63)
                    : static_cast<unsigned>(entropy_ % (32 - wired)) + wired;
}

std::uint64_t Cpu::read_control(unsigned index) {
  switch (index & 31) {
  case Random:
    return random_index();
  case Count:
    return count_ticks_ >> 1;
  case 7:
  case 21:
  case 22:
  case 23:
  case 24:
  case 25:
  case 31:
    return control_latch_;
  default:
    return control_[index & 31];
  }
}

void Cpu::write_control(unsigned index, std::uint64_t value) {
  index &= 31;
  control_latch_ = value;
  switch (index & 31) {
  case Index:
    control_[Index] = value & 0x8000003f;
    break;
  case EntryLo0:
  case EntryLo1:
    control_[index] = value & 0x3fffffff;
    break;
  case Context:
    control_[Context] = (value & 0xffffffffff800000ull) | (control_[Context] & 0x7ffff0);
    break;
  case PageMask:
    control_[PageMask] = value & 0x01ffe000;
    break;
  case Wired:
    control_[Wired] = value & 63;
    break;
  case Count:
    count_ticks_ = (value & 0xffffffff) << 1;
    break;
  case EntryHi:
    control_[EntryHi] = value & 0xc00000ffffffe0ffull;
    break;
  case Compare:
    control_[Compare] = value & 0xffffffff;
    set_interrupt(7, false);
    break;
  case Status:
    control_[Status] = (value & 0xff57ffff) | (control_[Status] & 0x00200000);
    interrupt_changed();
    break;
  case Cause:
    control_[Cause] = (control_[Cause] & ~0x300ull) | (value & 0x300);
    interrupt_changed();
    break;
  case Epc:
  case ErrorEpc:
    control_[index] = value;
    break;
  case Config:
    control_[Config] = (control_[Config] & ~0x0300800full) | (value & 0x0300800f);
    break;
  case LlAddr:
    control_[LlAddr] = value & 0xffffffff;
    break;
  case WatchLo:
    control_[WatchLo] = value & 0xfffffffb;
    break;
  case WatchHi:
    control_[WatchHi] = value & 15;
    break;
  case XContext:
    control_[XContext] = (value & 0xfffffffe00000000ull) | (control_[XContext] & 0x1fffffff0ull);
    break;
  case ParityError:
    control_[ParityError] = value & 255;
    break;
  case CacheError:
    control_[CacheError] = 0;
    break;
  case TagLo:
    control_[TagLo] = value & 0xffffffff;
    break;
  default:
    break;
  }
}

void Cpu::write_tlb(unsigned index) {
  if (index >= tlb_.size())
    return;
  auto &entry = tlb_[index];
  entry.mask = static_cast<std::uint32_t>(control_[PageMask]) & 0x01554000;
  entry.mask |= entry.mask >> 1;
  entry.hi = control_[EntryHi] & ~(std::uint64_t(entry.mask) | 0x1f00);
  entry.lo = {static_cast<std::uint32_t>(control_[EntryLo0] & 0x03ffffff),
              static_cast<std::uint32_t>(control_[EntryLo1] & 0x03ffffff)};
  const bool global = (entry.lo[0] & entry.lo[1] & 1) != 0;
  for (auto &lo : entry.lo)
    lo = (lo & ~1u) | static_cast<unsigned>(global);
}

void Cpu::cop0(std::uint32_t instruction) {
  const auto function = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto rd = (instruction >> 11) & 31;
  if (function == 2 || function == 6 || function == 8)
    return;
  if (mode() != Mode::Kernel && !(control_[Status] & 0x10000000)) {
    return raise(Exception::CoprocessorUnusable, 0);
  }
  if ((function == 1 || function == 5) && !require_doubleword())
    return;
  switch (function) {
  case 0:
    state_.gpr[rt] = sign_word(static_cast<std::uint32_t>(read_control(rd)));
    return;
  case 1:
    state_.gpr[rt] = read_control(rd);
    return;
  case 4:
  case 5:
    write_control(rd, state_.gpr[rt]);
    return;
  case 2:
  case 6:
  case 8:
    return;
  default:
    break;
  }
  if (function < 16)
    return raise(Exception::ReservedInstruction);
  switch (instruction & 63) {
  case 0x01: {
    const auto index = static_cast<unsigned>(control_[Index] & 63);
    if (index >= tlb_.size())
      return;
    const auto &entry = tlb_[index];
    control_[PageMask] = entry.mask;
    control_[EntryHi] = entry.hi;
    control_[EntryLo0] = entry.lo[0];
    control_[EntryLo1] = entry.lo[1];
    return;
  }
  case 0x02:
    return write_tlb(static_cast<unsigned>(control_[Index] & 63));
  case 0x06:
    return write_tlb(random_index());
  case 0x08:
    control_[Index] = 0x80000000;
    for (unsigned i = 0; i < tlb_.size(); ++i) {
      const auto &entry = tlb_[i];
      const auto mask = ~(std::uint64_t(entry.mask) | 0x1fff);
      if ((entry.hi & mask) != (control_[EntryHi] & mask))
        continue;
      if (!(entry.lo[0] & entry.lo[1] & 1) && (entry.hi & 255) != (control_[EntryHi] & 255))
        continue;
      control_[Index] = i;
      break;
    }
    return;
  case 0x18: {
    const bool error = control_[Status] & 4;
    const auto target = control_[error ? ErrorEpc : Epc];
    control_[Status] &= ~(error ? 4ull : 2ull);
    llbit_ = false;
    set_pc(target);
    block_exit_ = true;
    interrupt_changed();
    return;
  }
  default:
    return raise(Exception::ReservedInstruction);
  }
}

} // namespace cupid::n64
