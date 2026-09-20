#pragma once

#include "cupid/types.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cupid {

enum class GameBoyMapper { Linear, Mbc1, Mbc2, Mbc3, Mbc30, Mbc5 };

struct GameBoyCartridgeConfig {
    GameBoyMapper mapper{GameBoyMapper::Linear};
    unsigned ram_bytes{};
    bool clock{};
    bool rumble{};
};

struct GameBoyClock {
    u8 seconds{}, minutes{}, hours{};
    u16 days{};
    bool halted{}, carry{};
};

class GameBoyCartridge {
  public:
    static constexpr u64 cycles_per_second = 62'500'000;
    static std::optional<GameBoyCartridge> create(std::vector<u8> rom, GameBoyCartridgeConfig config,
                                                  std::string& error);
    void power();
    [[nodiscard]] u8 read(u16 address) const;
    void write(u16 address, u8 value);
    [[nodiscard]] std::span<u8> ram() {
        return ram_;
    }
    [[nodiscard]] std::span<const u8> ram() const {
        return ram_;
    }
    [[nodiscard]] bool rumble_active() const {
        return rumble_active_;
    }
    [[nodiscard]] GameBoyCartridgeConfig config() const {
        return config_;
    }
    [[nodiscard]] GameBoyClock clock() const {
        return clock_;
    }
    void set_clock(GameBoyClock clock);
    [[nodiscard]] bool clock_running() const {
        return config_.clock && !clock_.halted;
    }
    [[nodiscard]] u64 next_tick() const {
        return cycles_per_second - fraction_;
    }
    void tick(u64 cycles);

  private:
    GameBoyCartridge(std::vector<u8> rom, GameBoyCartridgeConfig config);
    GameBoyCartridgeConfig config_;
    std::vector<u8> rom_, ram_;
    unsigned rom_bank_{1}, ram_bank_{};
    bool ram_enabled_{}, banking_mode_{}, rumble_active_{};
    GameBoyClock clock_{}, latched_{};
    u8 latch_value_{};
    u64 fraction_{};
    [[nodiscard]] bool mbc3() const;
    [[nodiscard]] unsigned ram_offset(u16 address) const;
    [[nodiscard]] u8 read_clock() const;
    void write_clock(u8 value);
    void advance_seconds(u64 seconds);
};

} // namespace cupid
