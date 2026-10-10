#include "core/system/console.hpp"
#include <algorithm>
#include <ctime>

namespace cupid::n64 {
namespace {

ConsoleConfig configure(ConsoleConfig config) {
  if (config.arcade_profile != ArcadeProfile::Disabled) {
    config.expansion = true;
    config.region = VideoRegion::Ntsc;
    config.cic = CicModel::N5101;
    config.disk_drive = false;
  }
  return config;
}

} // namespace

Console::Console(ConsoleConfig config)
    : config_(configure(config)), ram_(ri_, random_, config_.expansion), mi_(ram_),
      cic_(config_.cic), pif_(cic_, ram_), si_(pif_, mi_, events_), pi_(ram_, mi_, events_),
      disk_(events_, config_.disk_clock), rsp_(ram_, mi_, random_), rdp_(ram_, rsp_, mi_),
      vi_(mi_, config_.region), audio_(ram_, mi_, config_.region), isviewer_(pi_),
      eeprom_(events_, config_.eeprom_size), rtc_(events_, config_.rtc_present, config_.rtc_clock),
      cartridge_joybus_(eeprom_, rtc_), sram_(config_.sram_size),
      flash_(events_, config_.flash_model),
      controllers_{Gamepad(random_), Gamepad(random_), Gamepad(random_), Gamepad(random_)},
      cpu_(*this, &random_), arcade_(config_.arcade_profile == ArcadeProfile::Disabled
                                         ? nullptr
                                         : std::make_unique<Aleck64>(config_.arcade_profile)) {
  mi_.connect([this](bool line) { cpu_.set_interrupt(2, line); }, [this] { frozen_ = true; });
  pif_.connect_reset([this] { cpu_.request_nmi(); });
  auto request_sync = [this] { clock_target_ = cpu_.state().clocks; };
  cpu_.connect_sync(request_sync);
  rsp_.connect_sync(request_sync);
  rdp_.connect_sync(request_sync);
  vi_.connect_sync(request_sync);
  audio_.connect_sync(request_sync);
  si_.connect_sync(request_sync);
  events_.connect_insert([this] {
    const auto remaining = std::max(0, events_.time_to_event());
    clock_target_ = std::min(clock_target_, cpu_.state().clocks + remaining);
  });
  pif_.attach(4, &cartridge_joybus_);
  if (arcade_)
    for (unsigned n = 0; n < 2; ++n)
      pif_.attach(n, &arcade_->controller(n));
  pi_.attach(rom_, 0);
  if (!sram_.data().empty())
    pi_.attach(sram_, 1);
  if (!flash_.data().empty())
    pi_.attach(flash_, 1);
  pi_.attach(isviewer_, 1);
  if (config_.disk_drive) {
    disk_.connect_interrupt([this](bool line) { cpu_.set_interrupt(3, line); });
    pi_.attach(disk_, 2);
  }
  rsp_.connect_display(
      [this](unsigned address) { return rdp_.read_word(address, rsp_.clocks(), false); },
      [this](unsigned address, std::uint32_t value) {
        rdp_.write_word(address, value, rsp_.clocks(), false);
      });
  power();
}

bool Console::load(std::span<const std::uint8_t> cartridge,
                   std::span<const std::uint8_t> firmware) {
  if (arcade_ && firmware.size() == 0x800)
    firmware = firmware.first(0x7c0);
  if (firmware.size() != 0x7c0)
    return false;
  if (cartridge.empty()) {
    if (!config_.disk_drive || !disk_.firmware_loaded())
      return false;
    rom_.disconnect();
  } else if (!rom_.load(cartridge)) {
    return false;
  }
  isviewer_.connect(rom_.data().size());
  pif_.load_rom(firmware);
  power();
  return true;
}

bool Console::load_disk(std::span<const std::uint8_t> ipl, std::span<const std::uint8_t> firmware,
                        std::span<const std::uint8_t> image) {
  if (!config_.disk_drive || firmware.size() != 0x7c0 || !disk_.load_ipl(ipl))
    return false;
  if (!image.empty() && !disk_.load_image(image))
    return false;
  return load({}, firmware);
}

void Console::power(bool reset) {
  if (!reset) {
    const auto seed = config_.random_seed     ? *config_.random_seed
                      : config_.entropy_clock ? config_.entropy_clock()
                                              : static_cast<std::uint64_t>(std::clock());
    random_.seed(seed);
  }
  events_.reset();
  flash_.power();
  isviewer_.power();
  if (arcade_)
    arcade_->power(reset);
  ram_.power(reset);
  mi_.power();
  vi_.power();
  audio_.power();
  pi_.power();
  pif_.power();
  cic_.power(config_.disk_drive && rom_.data().empty() && disk_.firmware_loaded() ? disk_.cic()
                                                                                  : config_.cic);
  ri_.power(reset);
  si_.power();
  cpu_.power();
  rsp_.power();
  rdp_.power();
  rtc_.power();
  if (config_.disk_drive)
    disk_.power();
  synchronized_clock_ = 0;
  clock_target_ = 0;
  frozen_ = false;
}

void Console::connect_controller(unsigned port, bool connected) {
  if (port < gamecube_controllers_.size())
    gamecube_controllers_[port].reset();
  if (arcade_ && port < 2)
    pif_.attach(port, connected ? &arcade_->controller(port) : nullptr);
  else if (port < controllers_.size())
    pif_.attach(port, connected ? &controllers_[port] : nullptr);
}

void Console::connect_mouse(unsigned port, bool connected) {
  if (port < mice_.size()) {
    gamecube_controllers_[port].reset();
    pif_.attach(port, connected ? &mice_[port] : nullptr);
  }
}

void Console::connect_gamecube_controller(unsigned port, bool connected) {
  if (port < gamecube_controllers_.size()) {
    gamecube_controllers_[port].reset();
    pif_.attach(port, connected ? &gamecube_controllers_[port] : nullptr);
  }
}

std::int64_t Console::pending_clocks() const {
  return static_cast<std::int64_t>(cpu_.state().clocks - synchronized_clock_);
}

void Console::step() {
  cpu_.step();
  synchronize();
}

std::uint32_t Console::run_interval(std::uint32_t limit) {
  const auto start = cpu_.state().clocks;
  const auto queue_limit = static_cast<std::uint64_t>(std::max(0, events_.time_to_event()));
  clock_target_ =
      start + std::min(std::uint64_t(limit), std::min(queue_limit, cpu_.synchronization_limit()));
  do {
    if (!cpu_.run_block(clock_target_)) {
      cpu_.step();
      break;
    }
  } while (cpu_.state().clocks < clock_target_);
  synchronize();
  return static_cast<std::uint32_t>(cpu_.state().clocks - start);
}

void Console::run_clocks(std::uint64_t clocks) {
  const auto start = cpu_.state().clocks;
  while (cpu_.state().clocks - start < clocks)
    step();
}

void Console::synchronize() {
  const auto clocks = static_cast<std::uint32_t>(pending_clocks());
  synchronized_clock_ = cpu_.state().clocks;
  cpu_.synchronize_timer([&] {
    vi_.elapse(clocks);
    audio_.elapse(clocks);
    rsp_.elapse(clocks);
    rdp_.elapse(clocks);
    pif_.elapse(clocks);
    vi_.run();
    audio_.run();
    rsp_.run();
    rdp_.run();
    pif_.run();
    events_.advance(clocks, [this](Event pending) { event(pending); });
  });
}

void Console::event(Event pending) {
  switch (pending) {
  case Event::PeripheralRead:
  case Event::PeripheralWrite:
    pi_.complete_dma();
    break;
  case Event::PeripheralBusWrite:
    pi_.complete_write();
    break;
  case Event::SerialRead:
    si_.dma_read();
    break;
  case Event::SerialWrite:
    si_.dma_write();
    break;
  case Event::SerialBusWrite:
    si_.complete_write();
    break;
  case Event::EepromWrite:
    eeprom_.complete_write();
    break;
  case Event::RtcTick:
    rtc_.tick();
    break;
  case Event::FlashComplete:
    flash_.complete();
    break;
  case Event::DiskClock:
  case Event::DiskResponse:
  case Event::DiskBlock:
  case Event::DiskMotor:
    disk_.event(pending);
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
