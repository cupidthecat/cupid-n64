#pragma once

#include "core/cartridge/eeprom.hpp"
#include "core/cartridge/rom.hpp"
#include "core/controller/gamepad.hpp"
#include "core/cpu/cpu.hpp"
#include "core/devices/audio/audio_interface.hpp"
#include "core/devices/pi/peripheral_interface.hpp"
#include "core/devices/si/serial_interface.hpp"
#include "core/devices/vi/video_interface.hpp"
#include "core/rdp/rdp.hpp"

namespace cupid::n64 {

struct ConsoleConfig {
  VideoRegion region = VideoRegion::Ntsc;
  bool expansion = true;
  CicModel cic = CicModel::N6102;
  unsigned eeprom_size = 0;
};

class Console : public Bus {
public:
  explicit Console(ConsoleConfig config = {});
  Console(const Console &) = delete;
  Console &operator=(const Console &) = delete;
  bool load(std::span<const std::uint8_t> cartridge, std::span<const std::uint8_t> firmware);
  void power();
  void step();
  std::uint32_t run_interval(std::uint32_t limit = 4096);
  void run_clocks(std::uint64_t clocks);
  void synchronize();
  void connect_controller(unsigned port, bool connected);
  bool frozen() const override {
    return frozen_;
  }
  BusRead read(std::uint32_t address, unsigned bytes) override;
  BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override;
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words) override;
  BusWrite write_burst(std::uint32_t address, std::span<const std::uint32_t> words) override;
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override;
  InstructionTracker *instruction_tracker() override {
    return ram_.instruction_tracker();
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
  Eeprom &eeprom() {
    return eeprom_;
  }

private:
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
  Rsp rsp_;
  Rdp rdp_;
  VideoInterface vi_;
  AudioInterface audio_;
  CartridgeRom rom_;
  Eeprom eeprom_;
  std::array<Gamepad, 4> controllers_;
  Cpu cpu_;
  std::uint64_t synchronized_clock_ = 0;
  std::uint64_t clock_target_ = 0;
  bool frozen_ = false;
};

} // namespace cupid::n64
