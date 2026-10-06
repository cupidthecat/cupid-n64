#include "core/cpu/cpu.hpp"
#include <algorithm>

namespace cupid::n64 {

Cpu::Cpu(Bus &bus) : bus_(bus) {
  power();
}

void Cpu::power() {
  state_ = {};
  control_ = {};
  tlb_ = {};
  icache_ = {};
  dcache_ = {};
  control_latch_ = cop2_latch_ = count_ticks_ = 0;
  entropy_ = 1;
  llbit_ = nmi_pending_ = false;
  control_[Status] = 0x3450ff04;
  control_[Config] = 0x7006e460;
  control_[PrId] = 0x0b22;
  state_.gpr[29] = 0xffffffffa4001ff0;
  set_pc(0xffffffffbfc00000);
}

void Cpu::set_pc(std::uint64_t address) {
  state_.pc = pipeline_pc_ = address;
  next_pc_ = address + 4;
  delay_slot_ = next_delay_slot_ = false;
}

Cpu::Mode Cpu::mode() const {
  if (control_[Status] & 6)
    return Mode::Kernel;
  return static_cast<Mode>(std::min(2u, static_cast<unsigned>((control_[Status] >> 3) & 3)));
}

bool Cpu::extended_addressing() const {
  const auto shift = mode() == Mode::Kernel ? 7 : mode() == Mode::Supervisor ? 6 : 5;
  return (control_[Status] >> shift) & 1;
}

bool Cpu::little_endian() const {
  const bool big = control_[Config] & 0x8000;
  const bool reverse = mode() == Mode::User && (control_[Status] & 0x02000000);
  return !(big ^ reverse);
}

bool Cpu::require_doubleword() {
  if (mode() == Mode::Kernel || extended_addressing())
    return true;
  raise(Exception::ReservedInstruction);
  return false;
}

void Cpu::advance_clocks(std::uint64_t clocks) {
  constexpr std::uint64_t mask = (1ull << 33) - 1;
  const auto ticks = (clocks + (state_.clocks & 1)) >> 1;
  const auto remaining = ((control_[Compare] << 1) - count_ticks_) & mask;
  if (remaining && ticks >= remaining)
    set_interrupt(7, true);
  count_ticks_ = (count_ticks_ + ticks) & mask;
  state_.clocks += clocks;
}

void Cpu::set_interrupt(unsigned bit, bool pending) {
  if (bit > 7)
    return;
  const auto mask = 1ull << (bit + 8);
  control_[Cause] = (control_[Cause] & ~mask) | (pending ? mask : 0);
}

void Cpu::request_nmi() {
  nmi_pending_ = true;
}

bool Cpu::poll_interrupt() {
  if ((control_[Status] & 7) == 1 && (control_[Status] & control_[Cause] & 0xff00)) {
    advance_clocks(2);
    raise(Exception::Interrupt);
    return true;
  }
  if (nmi_pending_) {
    nmi_pending_ = false;
    advance_clocks(2);
    control_[Status] = (control_[Status] | 0x00400004) & ~0x00300000ull;
    control_[ErrorEpc] = state_.pc;
    set_pc(0xffffffffbfc00000);
    return true;
  }
  return false;
}

void Cpu::begin_instruction() {
  next_delay_slot_ = false;
  pipeline_pc_ = next_pc_;
  next_pc_ += 4;
}

void Cpu::end_instruction() {
  state_.gpr[0] = 0;
  delay_slot_ = next_delay_slot_;
  state_.pc = pipeline_pc_;
}

void Cpu::step() {
  if (poll_interrupt())
    return;
  const auto instruction = read(state_.pc, 4, true);
  if (!instruction)
    return;
  begin_instruction();
  decode(static_cast<std::uint32_t>(*instruction));
  end_instruction();
}

void Cpu::execute(std::uint32_t instruction) {
  if (poll_interrupt())
    return;
  advance_clocks(2);
  begin_instruction();
  decode(instruction);
  end_instruction();
}

void Cpu::raise(Exception code, unsigned coprocessor, bool tlb_miss) {
  unsigned offset = 0x180;
  if (!(control_[Status] & 2)) {
    if (tlb_miss)
      offset = extended_addressing() ? 0x80 : 0;
    control_[Epc] = state_.pc - (delay_slot_ ? 4 : 0);
    control_[Cause] = (control_[Cause] & ~0x80000000ull) | (delay_slot_ ? 0x80000000ull : 0);
    control_[Status] |= 2;
  }
  control_[Cause] = (control_[Cause] & ~0x3000007cull) | (static_cast<std::uint64_t>(code) << 2) |
                    (static_cast<std::uint64_t>(coprocessor) << 28);
  const auto base = control_[Status] & 0x00400000 ? 0xffffffffbfc00200ull : 0xffffffff80000000ull;
  set_pc(base + offset);
}

void Cpu::address_exception(std::uint64_t address) {
  control_[BadVAddr] = address;
  control_[EntryHi] = (control_[EntryHi] & 0xff) | (address & 0xc00000ffffffe000ull);
  control_[Context] = (control_[Context] & 0xffffffffff800000ull) | ((address >> 9) & 0x7ffff0);
  control_[XContext] = (control_[XContext] & 0xfffffffe00000000ull) |
                       ((address >> 9) & 0x7ffffff0) | ((address >> 31) & 0x180000000ull);
}

void Cpu::jump(std::uint64_t target) {
  next_pc_ = target;
  next_delay_slot_ = true;
}

void Cpu::branch(bool taken, bool likely, std::int16_t offset) {
  if (taken)
    jump(pipeline_pc_ + static_cast<std::uint64_t>(std::int64_t(offset) * 4));
  else if (likely) {
    pipeline_pc_ += 4;
    next_pc_ = pipeline_pc_ + 4;
  } else
    next_delay_slot_ = true;
}

} // namespace cupid::n64
