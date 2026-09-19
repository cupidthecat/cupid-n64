#pragma once

#include "cupid/bus.hpp"
#include "cupid/cpu.hpp"
#include "cupid/rsp.hpp"
#include "cupid/types.hpp"

#include <filesystem>
#include <string>

namespace cupid {

enum class VideoStandard { Ntsc, Pal };

class System {
    VideoStandard video_standard_;

  public:
    explicit System(VideoStandard video_standard = VideoStandard::Ntsc);
    [[nodiscard]] u32 video_frequency() const {
        return video_standard_ == VideoStandard::Pal ? 49656530U : 48681818U;
    }
    void reset();
    void advance(u64 cpu_cycles);
    bool load_rom(const std::filesystem::path& path, std::string& error);
    bool load_pif(const std::filesystem::path& path, std::string& error);
    bool boot_cartridge(std::string& error);

    Cpu cpu;
    Rsp rsp;
    Bus bus;

  private:
    friend class Cpu;
    u64 rcp_fraction_{};
    [[nodiscard]] u64 cpu_cycles_for_rcp(u64 rcp_cycles) const;
    bool pif_loaded_{};
};

} // namespace cupid
