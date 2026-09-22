#include "cupid/system.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace cupid {
namespace {
constexpr u64 poll_clocks = 81920;
constexpr u64 boot_timeout = 6 * 62500000ULL;
} // namespace

void PifBoot::reset() {
    state_ = State::Lockout;
    rom_locked_ = false;
    os_info_.fill(0);
    checksum_.fill(0);
    timeout_ = phase_ = 0;
    bus_.pif[0x7e5] = static_cast<u8>(4U | (bus_.cic.disk_drive() ? 8U : 0U));
    bus_.pif[0x7e6] = bus_.pif[0x7e7] = bus_.cic.seed();
}

u64 PifBoot::next_poll() const {
    return (poll_clocks - phase_ + 2) / 3;
}

bool PifBoot::command_pending() const {
    const u8 command = bus_.pif[0x7ff];
    switch (state_) {
    case State::Lockout:
        return (command & 0x10U) != 0;
    case State::CaptureChecksum:
        return (command & 0x20U) != 0;
    case State::CheckChecksum:
        return (command & 0x40U) != 0;
    case State::Terminate:
        return (command & 0x08U) != 0;
    case State::Error:
        return true;
    case State::Running:
    case State::Resetting:
        return false;
    }
    return false;
}

u64 PifBoot::next_event() const {
    if (state_ == State::Resetting)
        return timeout_ != 0 ? timeout_ : !reset_button_ ? 1 : std::numeric_limits<u64>::max();
    u64 next = command_pending() ? next_poll() : std::numeric_limits<u64>::max();
    if (timeout_ != 0)
        next = std::min(next, timeout_);
    return next;
}

void PifBoot::tick(u64 rcp_cycles) {
    if (state_ == State::Resetting) {
        if (timeout_ != 0 && rcp_cycles != 0)
            bus_.pif[0x7ff] |= 0x80U;
        timeout_ -= std::min(timeout_, rcp_cycles);
        if (rcp_cycles != 0 && timeout_ == 0 && !reset_button_)
            warm_boot();
        return;
    }
    if (state_ == State::Running)
        return;
    const bool poll_due = rcp_cycles >= next_poll();
    phase_ = (phase_ + (rcp_cycles % poll_clocks) * 3) % poll_clocks;
    const bool expired = timeout_ != 0 && rcp_cycles >= timeout_;
    timeout_ -= std::min(timeout_, rcp_cycles);
    if (poll_due || expired)
        poll();
    if (expired && state_ == State::Terminate) {
        state_ = State::Error;
        bus_.system_.cpu.request_nmi();
    }
}

void PifBoot::poll() {
    u8& command = bus_.pif[0x7ff];
    switch (state_) {
    case State::Lockout:
        if ((command & 0x10U) == 0)
            return;
        rom_locked_ = true;
        bus_.joybus.reset();
        state_ = State::CaptureChecksum;
        return;
    case State::CaptureChecksum:
        if ((command & 0x20U) == 0)
            return;
        swap_secrets();
        command |= 0x80U;
        state_ = State::CheckChecksum;
        return;
    case State::CheckChecksum:
        if ((command & 0x40U) == 0)
            return;
        command &= 0x0fU;
        os_info_[0] |= 2U;
        if (!bus_.cic.verify_checksum(checksum_)) {
            state_ = State::Error;
            checksum_.fill(0);
            bus_.system_.cpu.request_nmi();
            return;
        }
        checksum_.fill(0);
        state_ = State::Terminate;
        timeout_ = boot_timeout;
        return;
    case State::Terminate:
        if ((command & 0x08U) == 0)
            return;
        command = 0;
        timeout_ = 0;
        state_ = State::Running;
        return;
    case State::Error:
        bus_.system_.cpu.request_nmi();
        return;
    case State::Running:
    case State::Resetting:
        return;
    }
}

void PifBoot::swap_secrets() {
    const u8 external = bus_.pif[0x7e5];
    bus_.pif[0x7e5] = static_cast<u8>((external & 0xf0U) | (os_info_[0] & 0x0fU));
    os_info_[0] = external & 0x0fU;
    for (unsigned i = 1; i < os_info_.size(); ++i)
        std::swap(bus_.pif[0x7e5 + i], os_info_[i]);
    for (unsigned i = 0; i < checksum_.size(); ++i)
        std::swap(bus_.pif[0x7f2 + i], checksum_[i]);
}
} // namespace cupid
