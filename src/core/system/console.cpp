#include "core/system/console.hpp"
#include <algorithm>

namespace cupid::n64 {

Console::Console(ConsoleConfig config)
    : config_(config), ram_(ri_, random_, config.expansion), mi_(ram_), cic_(config.cic),
      pif_(cic_, ram_), si_(pif_, mi_, events_), pi_(ram_, mi_, events_), rsp_(ram_, mi_, random_),
      rdp_(ram_, rsp_, mi_), vi_(mi_, config.region), audio_(ram_, mi_, config.region),
      eeprom_(events_, config.eeprom_size),
      controllers_{Gamepad(random_), Gamepad(random_), Gamepad(random_), Gamepad(random_)},
      cpu_(*this) {
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
  pif_.attach(4, &eeprom_);
  pi_.attach(rom_, 0);
  rsp_.connect_display(
      [this](unsigned address) { return rdp_.read_word(address, rsp_.clocks(), false); },
      [this](unsigned address, std::uint32_t value) {
        rdp_.write_word(address, value, rsp_.clocks(), false);
      });
  power();
}

bool Console::load(std::span<const std::uint8_t> cartridge,
                   std::span<const std::uint8_t> firmware) {
  if (firmware.size() != 0x7c0 || !rom_.load(cartridge))
    return false;
  pif_.load_rom(firmware);
  power();
  return true;
}

void Console::power() {
  random_.seed(0);
  events_.reset();
  eeprom_.complete_write();
  ram_.power();
  mi_.power();
  vi_.power();
  audio_.power();
  pi_.power();
  pif_.power();
  cic_.power(config_.cic);
  ri_.power();
  si_.power();
  cpu_.power();
  rsp_.power();
  rdp_.power();
  synchronized_clock_ = 0;
  clock_target_ = 0;
  frozen_ = false;
}

void Console::connect_controller(unsigned port, bool connected) {
  if (port < controllers_.size())
    pif_.attach(port, connected ? &controllers_[port] : nullptr);
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
      start + std::min({std::uint64_t(limit), queue_limit, cpu_.synchronization_limit()});
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
  default:
    break;
  }
}

} // namespace cupid::n64
