#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {
class Bus;

class PifBoot {
  public:
    explicit PifBoot(Bus& bus) : bus_(bus) {}
    void reset();
    void set_reset_button(bool pressed);
    [[nodiscard]] bool pre_nmi() const {
        return state_ == State::Resetting;
    }
    void tick(u64 rcp_cycles);
    [[nodiscard]] u64 next_event() const;
    [[nodiscard]] bool rom_locked() const {
        return rom_locked_;
    }
    [[nodiscard]] bool failed() const {
        return state_ == State::Error;
    }

  private:
    enum class State { Lockout, CaptureChecksum, CheckChecksum, Terminate, Running, Resetting, Error };
    Bus& bus_;
    State state_{State::Lockout};
    bool rom_locked_{};
    bool reset_button_{};
    std::array<u8, 3> os_info_{};
    std::array<u8, 6> checksum_{};
    u64 timeout_{};
    u64 phase_{};
    [[nodiscard]] u64 next_poll() const;
    [[nodiscard]] bool command_pending() const;
    void poll();
    void swap_secrets();
    void warm_boot();
};
} // namespace cupid
