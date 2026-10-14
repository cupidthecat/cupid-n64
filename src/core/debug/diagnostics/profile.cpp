#include "core/cpu/cpu.hpp"
#include "core/debug/diagnostics.hpp"

namespace cupid::n64 {

ProfileSnapshot Diagnostics::profile() const noexcept {
  auto result = profile_;
  if (profiling() && cpu_) {
    const auto clock = cpu_->state_.clocks + cpu_->profile_deferred_clocks_;
    if (clock < cpu_profile_clock_)
      return result;
    const auto ticks = (clock - cpu_profile_clock_) >> 1;
    result.cpu[0] += ticks;
    if (cpu_->control_[Status] & 2)
      result.cpu[1] += ticks;
  }
  return result;
}

void Diagnostics::synchronize_cpu(bool discard_remainder) noexcept {
  if (!cpu_)
    return;
  profile_ = profile();
  const auto clock = cpu_->state_.clocks + cpu_->profile_deferred_clocks_;
  if (discard_remainder || clock < cpu_profile_clock_)
    cpu_profile_clock_ = clock;
  else
    cpu_profile_clock_ += ((clock - cpu_profile_clock_) >> 1) << 1;
}

std::uint64_t ProfileSnapshot::read(unsigned code) const {
  if (code <= 1)
    return cpu[code];
  if (code >= 0x10 && code <= 0x12)
    return cpu[2 + code - 0x10];
  if (code >= 0x20 && code <= 0x22)
    return cpu[5 + code - 0x20];
  if (code >= 0x200 && code <= 0x201)
    return rsp[code - 0x200];
  const auto operation = code & 15;
  const auto device = (code >> 4) & 15;
  if ((code & ~0xffu) != 0x300 || operation > 2 || device > traffic.size())
    return 0;
  TrafficMetric total;
  if (device)
    total = traffic[device - 1];
  else
    for (const auto &metric : traffic) {
      total.reads += metric.reads;
      total.writes += metric.writes;
    }
  return operation == 1 ? total.reads : operation == 2 ? total.writes : total.reads + total.writes;
}

ProfileSnapshot &ProfileSnapshot::operator+=(const ProfileSnapshot &other) {
  for (unsigned n = 0; n < cpu.size(); ++n)
    cpu[n] += other.cpu[n];
  for (unsigned n = 0; n < rsp.size(); ++n)
    rsp[n] += other.rsp[n];
  for (unsigned n = 0; n < traffic.size(); ++n) {
    traffic[n].reads += other.traffic[n].reads;
    traffic[n].writes += other.traffic[n].writes;
  }
  return *this;
}

ProfileSnapshot &ProfileSnapshot::operator-=(const ProfileSnapshot &other) {
  for (unsigned n = 0; n < cpu.size(); ++n)
    cpu[n] -= other.cpu[n];
  for (unsigned n = 0; n < rsp.size(); ++n)
    rsp[n] -= other.rsp[n];
  for (unsigned n = 0; n < traffic.size(); ++n) {
    traffic[n].reads -= other.traffic[n].reads;
    traffic[n].writes -= other.traffic[n].writes;
  }
  return *this;
}

void Diagnostics::cpu_cycles(std::uint64_t ticks, bool exception) noexcept {
  if (!profiling())
    return;
  profile_.cpu[0] += ticks;
  if (exception)
    profile_.cpu[1] += ticks;
}

void Diagnostics::commit_cpu_cycles(std::uint64_t ticks, bool exception) noexcept {
  committed_cpu_cycles_[0] += ticks;
  if (exception)
    committed_cpu_cycles_[1] += ticks;
}

ProfileSnapshot Diagnostics::guest_profile() const noexcept {
  auto result = profile_;
  result.cpu[0] = committed_cpu_cycles_[0];
  result.cpu[1] = committed_cpu_cycles_[1];
  return result;
}

void Diagnostics::cache_access(bool instruction, bool hit) noexcept {
  if (profiling())
    ++profile_.cpu[(instruction ? 2 : 5) + (hit ? 0 : 1)];
}

void Diagnostics::cache_writeback(bool instruction) noexcept {
  if (profiling())
    ++profile_.cpu[instruction ? 4 : 7];
}

void Diagnostics::rsp_cycles(std::uint64_t clocks, bool halted) noexcept {
  if (!profiling())
    return;
  profile_.rsp[0] += clocks;
  if (halted)
    profile_.rsp[1] += clocks;
}

void Diagnostics::traffic(MemoryRequestor requestor, unsigned bytes, bool write) noexcept {
  const auto index = static_cast<unsigned>(requestor);
  if (!profiling() || index >= profile_.traffic.size())
    return;
  auto &metric = profile_.traffic[index];
  (write ? metric.writes : metric.reads) += bytes;
}

void Diagnostics::profile_command(std::uint64_t slot, unsigned command) {
  if (command == 4) {
    slots_.clear();
    return;
  }
  if (slot >= 1024)
    return;
  if (slots_.size() <= slot)
    slots_.resize(static_cast<std::size_t>(slot + 1));
  if (command == 1)
    slots_[slot] -= guest_profile();
  if (command == 2)
    slots_[slot] += guest_profile();
  if (command == 3)
    slots_[slot] = {};
}

std::uint64_t Diagnostics::profile_read(std::uint64_t slot, unsigned code) const {
  if (slot & (1ull << 63))
    return guest_profile().read(code);
  return slot < slots_.size() ? slots_[slot].read(code) : 0;
}

} // namespace cupid::n64
