#pragma once

#include "core/arcade/aleck64.hpp"
#include "core/cartridge/flash/flash.hpp"
#include "core/cartridge/isviewer/isviewer.hpp"
#include "core/cartridge/joybus.hpp"
#include "core/cartridge/rom.hpp"
#include "core/cartridge/sram.hpp"
#include "core/controller/gamecube/gamecube.hpp"
#include "core/controller/gamepad.hpp"
#include "core/controller/mouse/mouse.hpp"
#include "core/cpu/cpu.hpp"
#include "core/devices/audio/audio_interface.hpp"
#include "core/devices/pi/peripheral_interface.hpp"
#include "core/devices/si/serial_interface.hpp"
#include "core/devices/vi/video_interface.hpp"
#include "core/disk/drive.hpp"
#include "core/rdp/rdp.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace cupid::n64 {

struct ConsoleConfig {
  VideoRegion region = VideoRegion::Ntsc;
  bool expansion = true;
  CicModel cic = CicModel::N6102;
  unsigned eeprom_size = 0;
  unsigned sram_size = 0;
  std::optional<FlashModel> flash_model = {};
  bool rtc_present = false;
  Rtc::HostClock rtc_clock = {};
  bool disk_drive = false;
  DiskClock::HostClock disk_clock = {};
  ArcadeProfile arcade_profile = ArcadeProfile::Disabled;
  // Cold boots use this seed when present, otherwise the host clock.
  std::optional<std::uint64_t> random_seed = {};
  std::function<std::uint64_t()> entropy_clock = {};
};

class Console : public Bus {
public:
  explicit Console(ConsoleConfig config = {});
  Console(const Console &) = delete;
  Console &operator=(const Console &) = delete;
  bool load(std::span<const std::uint8_t> cartridge, std::span<const std::uint8_t> firmware);
  bool load_disk(std::span<const std::uint8_t> ipl, std::span<const std::uint8_t> firmware,
                 std::span<const std::uint8_t> image = {});
  void power(bool reset = false);
  void step();
  std::uint32_t run_interval(std::uint32_t limit = 4096);
  void run_clocks(std::uint64_t clocks);
  void synchronize();
  void connect_controller(unsigned port, bool connected);
  void connect_mouse(unsigned port, bool connected = true);
  void connect_gamecube_controller(unsigned port, bool connected = true);
  bool frozen() const override {
    return frozen_;
  }
  BusRead read(std::uint32_t address, unsigned bytes) override;
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override;
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words) override;
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> words) override;
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override;
  std::span<const std::uint32_t> cache_fill_data(std::uint32_t address) const override;
  bool instruction_coherent(std::uint32_t address, std::span<const std::uint32_t> words) override;
  InstructionTracker *instruction_tracker() override {
    return arcade_ ? nullptr : ram_.instruction_tracker();
  }
  Cpu &cpu() {
    return cpu_;
  }
  Rdram &ram() {
    return ram_;
  }
  MipsInterface &interrupts() {
    return mi_;
  }
  Rsp &signal() {
    return rsp_;
  }
  Rdp &display() {
    return rdp_;
  }
  VideoInterface &video() {
    return vi_;
  }
  AudioInterface &audio() {
    return audio_;
  }
  Pif &pif() {
    return pif_;
  }
  Gamepad &controller(unsigned port) {
    return controllers_.at(port);
  }
  Mouse &mouse(unsigned port) {
    return mice_.at(port);
  }
  GameCubePad &gamecube_controller(unsigned port) {
    return gamecube_controllers_.at(port);
  }
  Eeprom &eeprom() {
    return eeprom_;
  }
  Rtc &rtc() {
    return rtc_;
  }
  Sram &sram() {
    return sram_;
  }
  FlashRam &flash() {
    return flash_;
  }
  DiskDrive &disk_drive() {
    return disk_;
  }
  Aleck64 *arcade() {
    return arcade_.get();
  }

private:
  friend class CoreState;
  std::uint32_t read_register(std::uint32_t address);
  void write_register(std::uint32_t address, std::uint32_t value);
  std::int64_t pending_clocks() const;
  void event(Event event);
  ConsoleConfig config_;
  RandomGenerator random_;
  RamInterface ri_;
  Rdram ram_;
  MipsInterface mi_;
  EventQueue events_;
  Cic cic_;
  Pif pif_;
  SerialInterface si_;
  PeripheralInterface pi_;
  DiskDrive disk_;
  Rsp rsp_;
  Rdp rdp_;
  VideoInterface vi_;
  AudioInterface audio_;
  CartridgeRom rom_;
  IsViewer isviewer_;
  Eeprom eeprom_;
  Rtc rtc_;
  CartridgeJoybus cartridge_joybus_;
  Sram sram_;
  FlashRam flash_;
  std::array<Gamepad, 4> controllers_;
  std::array<Mouse, 4> mice_;
  std::array<GameCubePad, 4> gamecube_controllers_;
  Cpu cpu_;
  std::uint64_t synchronized_clock_ = 0;
  std::uint64_t clock_target_ = 0;
  bool frozen_ = false;
  std::unique_ptr<Aleck64> arcade_;
};

} // namespace cupid::n64
