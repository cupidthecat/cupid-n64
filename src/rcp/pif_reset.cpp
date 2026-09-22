#include "cupid/system.hpp"

namespace cupid {

void PifBoot::set_reset_button(bool pressed) {
    const bool edge = pressed && !reset_button_;
    reset_button_ = pressed;
    if (!edge || state_ != State::Running)
        return;
    state_ = State::Resetting;
    timeout_ = 31250000;
    bus_.pif[0x7ff] |= 0x80U;
}

void PifBoot::warm_boot() {
    rom_locked_ = false;
    state_ = State::Lockout;
    timeout_ = phase_ = 0;
    swap_secrets();
    bus_.pif[0x7ff] = 0;
    bus_.system_.cpu.request_nmi();
}

} // namespace cupid
