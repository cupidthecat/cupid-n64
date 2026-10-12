#include "core/debug/debugger.hpp"
#include "core/system/console.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cupid::n64 {

Debugger::Debugger(Console &console) : console_(console) {}
Debugger::~Debugger() {
  detach();
}

std::uint64_t Debugger::canonical(std::uint64_t address) const {
  return !console_.cpu().extended_addressing() && address >= 0x80000000 && address <= 0xffffffff
             ? address | 0xffffffff00000000
             : address;
}

void Debugger::attach() {
  if (attached_)
    return;
  if (console_.cpu().debugger_)
    throw std::runtime_error("A debugger is already attached.");
  console_.cpu().debugger_ = this;
  attached_ = true;
  halt(DebugStopKind::Attached);
}

void Debugger::detach() {
  if (console_.cpu().debugger_ == this)
    console_.cpu().debugger_ = nullptr;
  attached_ = halted_ = stepping_ = false;
  points_.clear();
  skip_breakpoint_.reset();
}

void Debugger::halt(DebugStopKind kind, std::uint64_t address) {
  halted_ = true;
  stepping_ = false;
  stop_ = {kind, console_.cpu().state().pc, address};
}

void Debugger::interrupt() {
  if (attached_)
    halt(DebugStopKind::Interrupt);
}

void Debugger::resume(bool step, std::optional<std::uint64_t> pc) {
  if (!attached_)
    return;
  if (pc)
    console_.cpu().set_pc(canonical(*pc));
  skip_breakpoint_ = console_.cpu().state().pc;
  stepping_ = step;
  halted_ = false;
}

bool Debugger::point(DebugPointKind kind, std::uint64_t address, unsigned length, bool insert) {
  if (!attached_ || !length || length > maximum_transfer ||
      static_cast<unsigned>(kind) > static_cast<unsigned>(DebugPointKind::Access))
    return false;
  address = canonical(address);
  if (length - 1 > std::numeric_limits<std::uint64_t>::max() - address ||
      ((kind == DebugPointKind::Software || kind == DebugPointKind::Hardware) &&
       ((address & 3) || length != 4)))
    return false;
  const Point value{kind, address, length};
  const auto found = std::find(points_.begin(), points_.end(), value);
  if (!insert) {
    if (found != points_.end())
      points_.erase(found);
    return true;
  }
  if (found != points_.end())
    return true;
  if (points_.size() >= maximum_points)
    return false;
  points_.push_back(value);
  return true;
}

void Debugger::advance(unsigned clocks) {
  if (!attached_ || halted_)
    return;
  const auto start = console_.cpu().state().clocks;
  const auto frame = console_.video().frames();
  do {
    const auto pc = console_.cpu().state().pc;
    if (skip_breakpoint_ != pc) {
      for (const auto &point : points_)
        if ((point.kind == DebugPointKind::Software || point.kind == DebugPointKind::Hardware) &&
            point.address == pc) {
          halt(DebugStopKind::Breakpoint);
          return;
        }
    }
    skip_breakpoint_.reset();
    const bool step = stepping_;
    console_.step();
    if (halted_)
      stop_.pc = console_.cpu().state().pc;
    else if (step)
      halt(DebugStopKind::Step);
    if (console_.frozen() || console_.diagnostics().control() != HostControl::None)
      break;
  } while (!halted_ && console_.cpu().state().clocks - start < clocks &&
           console_.video().frames() == frame);
}

void Debugger::memory_access(std::uint64_t address, unsigned bytes, bool write) {
  if (!attached_ || halted_ || !bytes)
    return;
  for (const auto &point : points_) {
    if (point.kind == DebugPointKind::Software || point.kind == DebugPointKind::Hardware ||
        (write && point.kind == DebugPointKind::Read) ||
        (!write && point.kind == DebugPointKind::Write))
      continue;
    const auto intersects = [](std::uint64_t a, unsigned a_size, std::uint64_t b, unsigned b_size) {
      return a <= b ? b - a < a_size : a - b < b_size;
    };
    bool hit = intersects(address, bytes, point.address, point.length);
    for (unsigned accessed = 0; !hit && accessed < bytes;) {
      const auto current = address + accessed;
      const auto access_size = std::min(bytes - accessed, 4096u - unsigned(current & 4095));
      const auto access = console_.cpu().inspect_address(current & ~3ull);
      for (unsigned watched = 0; access && !hit && watched < point.length;) {
        const auto target_address = point.address + watched;
        const auto target_size =
            std::min(point.length - watched, 4096u - unsigned(target_address & 4095));
        const auto target = console_.cpu().inspect_address(target_address & ~3ull);
        if (target)
          hit = intersects(access->physical | (current & 3), access_size,
                           target->physical | (target_address & 3), target_size);
        watched += target_size;
      }
      accessed += access_size;
    }
    if (hit) {
      halt(point.kind == DebugPointKind::Access ? DebugStopKind::AccessWatch
           : write                              ? DebugStopKind::WriteWatch
                                                : DebugStopKind::ReadWatch,
           point.address);
      return;
    }
  }
}

} // namespace cupid::n64
