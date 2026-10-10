#pragma once

#include "core/devices/cic/cic.hpp"
#include "core/devices/joybus/device.hpp"
#include "core/devices/rdram/rdram.hpp"
#include <functional>

namespace cupid::n64 {

class Pif {
public:
  enum class State { Init, Lockout, GetChecksum, CheckChecksum, Terminate, Run, Error };
  Pif(Cic &cic, Rdram &ram);
  bool load_rom(std::span<const std::uint8_t> data);
  void power();
  void connect_reset(std::function<void()> callback);
  void attach(unsigned channel, JoybusDevice *device);
  void advance(std::uint32_t clocks);
  void elapse(std::uint32_t clocks) {
    clock_ -= clocks;
  }
  void run();
  void tick();
  std::uint32_t read_word(std::uint32_t address);
  void write_word(std::uint32_t address, std::uint32_t value);
  void dma_read(std::uint32_t address, std::uint32_t dram_address);
  void dma_write(std::uint32_t address, std::uint32_t dram_address);
  std::uint32_t estimate_timing() const;
  State state() const {
    return state_;
  }
  bool reset_enabled() const {
    return reset_enabled_;
  }
  std::span<std::uint8_t, 64> ram() {
    return ram_;
  }

private:
  friend class CoreState;
  std::uint32_t read_internal(std::uint32_t address) const;
  void write_internal(std::uint32_t address, std::uint32_t value);
  void access(bool read, bool wide);
  void swap_secrets();
  void joy_init();
  void joy_parse();
  void joy_run();
  void challenge();
  static void descramble(std::span<std::uint8_t> data);
  Cic &cic_;
  Rdram &dram_;
  std::function<void()> reset_;
  std::array<std::uint8_t, 0x7c0> rom_{};
  std::array<std::uint8_t, 64> ram_{};
  std::array<std::uint8_t, 3> os_info_{};
  std::array<std::uint8_t, 6> cpu_checksum_{};
  std::array<std::uint8_t, 6> cic_checksum_{};
  std::array<std::uint8_t, 5> joy_address_{};
  std::array<bool, 5> joy_skip_{};
  std::array<bool, 5> joy_reset_{};
  std::array<JoybusDevice *, 5> devices_{};
  std::int64_t clock_ = 0;
  std::int32_t timeout_ = 0;
  State state_ = State::Init;
  bool locked_ = false;
  bool reset_enabled_ = false;
};

} // namespace cupid::n64
