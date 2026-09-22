#pragma once

#include "cupid/bus.hpp"
#include "cupid/cpu.hpp"
#include "cupid/rsp.hpp"
#include "cupid/types.hpp"

#include <filesystem>
#include <span>
#include <string>

namespace cupid {

enum class VideoStandard { Ntsc, Pal };

class System {
    VideoStandard video_standard_;
    // Device time owed by the CPU. It sits before the devices so Bus::reset can clear it
    // while the System is still under construction.
    u64 deferred_rcp_{};
    u64 peripheral_debt_{};
    u64 defer_limit_{};
    bool settling_{};

  public:
    explicit System(VideoStandard video_standard = VideoStandard::Ntsc);
    [[nodiscard]] VideoStandard video_standard() const {
        return video_standard_;
    }
    [[nodiscard]] u32 video_frequency() const {
        return video_standard_ == VideoStandard::Pal ? 49656530U : 48681818U;
    }
    void reset();
    void set_reset_button(bool pressed);
    void advance(u64 cpu_cycles);
    // Devices run behind the CPU while it executes from its caches. Every read or
    // write of device state first brings the devices up to the CPU's clock.
    void settle() {
        if (settling_)
            return;
        if (deferred_rcp_ != 0)
            settle_deferred();
        if (peripheral_debt_ != 0)
            settle_peripherals();
    }
    bool load_rom(const std::filesystem::path& path, std::string& error);
    bool load_pif(const std::filesystem::path& path, std::string& error);
    bool load_pif(std::span<const u8> bytes, std::string& error);
    bool boot_cartridge(std::string& error);

    Cpu cpu;
    Rsp rsp;
    Bus bus;

  private:
    friend class Cpu;
    friend class Rsp;
    friend class Bus;
    u64 rcp_fraction_{};
    u64 event_gap_{};
    bool event_valid_{};
    void advance_deferred(u64 cpu_cycles);
    void settle_deferred();
    void settle_peripherals();
    [[nodiscard]] u64 cpu_cycles_for_rcp(u64 rcp_cycles) const;
    [[nodiscard]] bool reusable_deferred_event_cycles(u64& cpu_cycles) const;
    [[nodiscard]] u64 idle_loop_event_cycles();
    [[nodiscard]] u64 cached_private_event_cycles();
    [[nodiscard]] bool rsp_local_execution_ready() const;
    [[nodiscard]] bool run_local_rsp_tick();
    [[nodiscard]] u64 run_local_rsp_for_idle(u64 maximum_cpu_cycles);
    void advance_after_local_rsp(u64 cpu_cycles);
    bool pif_loaded_{};
};

} // namespace cupid
