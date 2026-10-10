#include "core/controller/accessories/cartridge/implementation.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"

namespace cupid::n64 {

void CoreState::visit(state::Archive &a, Gamepad &p) {
  // Accessory connections and host clocks remain owned by the live console.
  a.identity(p.pak_);
  a.fixed_vector(p.ram_);
  a.bounded(p.bank_, 0u, p.ram_.empty() ? 0u : static_cast<unsigned>(p.ram_.size() / 0x8000 - 1));
  a.fields(p.buttons_, p.x_, p.y_, p.detect_, p.motor_);
  visit(a, p.transfer_);
  visit(a, p.sensor_);
}

void CoreState::visit(state::Archive &a, Mouse &m) {
  a.fields(m.x_, m.y_, m.left_, m.right_);
}

void CoreState::visit(state::Archive &a, GameCubePad &p) {
  a.fields(p.input_.buttons, p.input_.x, p.input_.y, p.input_.cx, p.input_.cy, p.input_.l,
           p.input_.r);
  a.array(p.analog_);
  a.fields(p.buttons_, p.origin_pending_, p.rumbling_);
}

void CoreState::visit(state::Archive &a, BioSensor &s) {
  a.identity(bool(s.clock_));
  a.fields(s.next_, s.start_, s.pulsing_);
  a.bounded(s.bpm_, 30u, 180u);
}

void CoreState::visit(state::Archive &a, TransferPak &p) {
  a.identity(bool(p.cartridge_));
  a.bounded(p.bank_, 0u, 3u);
  a.bounded(p.reset_, 0u, 3u);
  a.fields(p.enabled_, p.cartridge_enabled_, p.empty_board_);
  if (p.cartridge_) {
    auto *cartridge = dynamic_cast<HandheldCartridge *>(p.cartridge_.get());
    state::Archive::require(cartridge != nullptr);
    visit(a, *cartridge);
  }
}

void CoreState::visit(state::Archive &a, HandheldCartridge &cartridge) {
  auto &c = *cartridge.implementation_;
  a.identity(c.config.board);
  a.identity(c.config.ram_size);
  a.identity(c.config.rtc);
  a.identity(c.config.eeprom_size);
  a.identity(c.rom_size);
  a.bytes_identity(c.rom);
  a.fixed_vector(c.ram);
  a.fixed_vector(c.flash);
  a.fixed_vector(c.eeprom);
  a.fields(c.rom_bank, c.ram_bank, c.base_bank, c.mode, c.ram_enabled, c.extra_enabled, c.rumble,
           c.board_present);
  a.array(c.region_bank);
  a.array(c.ram_region);
  a.array(c.region_flash);
  a.fields(c.flash_enabled, c.flash_writable);
  a.array(c.time);
  a.array(c.latched);
  a.fields(c.latch, c.motion_x, c.motion_y, c.tilt_x, c.tilt_y, c.select, c.input, c.output,
           c.index, c.ready, c.time_index);
  a.array(c.calendar);
  a.fields(c.serial_select, c.serial_clock, c.serial_writable, c.serial_input, c.serial_bits,
           c.serial_output, c.serial_remaining, c.serial_address, c.serial_busy, c.serial_mode);
}

void CoreState::visit(state::Archive &a, Aleck64 &c) {
  a.identity(c.profile_);
  a.fixed_vector(c.sdram_);
  a.array(c.video_ram_);
  a.array(c.palette_ram_);
  for (auto &p : c.input_.players) {
    a.fields(p.up, p.down, p.left, p.right, p.start, p.coin, p.x, p.y);
    a.array(p.buttons);
  }
  a.fields(c.input_.service, c.input_.test, c.input_.mahjong);
  a.array(c.dip_switches_);
  a.fields(c.mahjong_row_, c.video_enabled_);
}

} // namespace cupid::n64
