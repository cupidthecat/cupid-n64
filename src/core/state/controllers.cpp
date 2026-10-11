#include "core/controller/accessories/cartridge/implementation.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"

namespace cupid::n64 {

void CoreState::visit(state::Archive &a, Gamepad &p, unsigned port) {
  // Accessory connections and host clocks remain owned by the live console.
  a.indexed_label("controller", port, "pak", sizeof(p.pak_));
  a.identity(p.pak_);
  a.indexed_fixed_vector(p.ram_, "controller", port, "ram");
  a.indexed_label("controller", port, "bank", sizeof(p.bank_));
  a.bounded(p.bank_, 0u, p.ram_.empty() ? 0u : static_cast<unsigned>(p.ram_.size() / 0x8000 - 1));
  a.indexed_label("controller", port, "buttons", sizeof(p.buttons_));
  a.field(p.buttons_);
  a.indexed_label("controller", port, "x", sizeof(p.x_));
  a.field(p.x_);
  a.indexed_label("controller", port, "y", sizeof(p.y_));
  a.field(p.y_);
  a.indexed_label("controller", port, "detect", sizeof(p.detect_));
  a.field(p.detect_);
  a.indexed_label("controller", port, "motor", sizeof(p.motor_));
  a.field(p.motor_);
  visit(a, p.transfer_, port);
  visit(a, p.sensor_, port);
}

void CoreState::visit(state::Archive &a, Mouse &m, unsigned port) {
  a.indexed_label("mouse", port, "x", sizeof(m.x_));
  a.field(m.x_);
  a.indexed_label("mouse", port, "y", sizeof(m.y_));
  a.field(m.y_);
  a.indexed_label("mouse", port, "left", sizeof(m.left_));
  a.field(m.left_);
  a.indexed_label("mouse", port, "right", sizeof(m.right_));
  a.field(m.right_);
}

void CoreState::visit(state::Archive &a, GameCubePad &p, unsigned port) {
  a.indexed_label("gamecube", port, "input.buttons", sizeof(p.input_.buttons));
  a.field(p.input_.buttons);
  a.indexed_label("gamecube", port, "input.x", sizeof(p.input_.x));
  a.field(p.input_.x);
  a.indexed_label("gamecube", port, "input.y", sizeof(p.input_.y));
  a.field(p.input_.y);
  a.indexed_label("gamecube", port, "input.cx", sizeof(p.input_.cx));
  a.field(p.input_.cx);
  a.indexed_label("gamecube", port, "input.cy", sizeof(p.input_.cy));
  a.field(p.input_.cy);
  a.indexed_label("gamecube", port, "input.l", sizeof(p.input_.l));
  a.field(p.input_.l);
  a.indexed_label("gamecube", port, "input.r", sizeof(p.input_.r));
  a.field(p.input_.r);
  a.indexed_label("gamecube", port, "analog", sizeof(p.analog_[0]));
  a.array(p.analog_);
  a.indexed_label("gamecube", port, "buttons", sizeof(p.buttons_));
  a.field(p.buttons_);
  a.indexed_label("gamecube", port, "origin_pending", sizeof(p.origin_pending_));
  a.field(p.origin_pending_);
  a.indexed_label("gamecube", port, "rumbling", sizeof(p.rumbling_));
  a.field(p.rumbling_);
}

void CoreState::visit(state::Archive &a, BioSensor &s, unsigned port) {
  a.indexed_label("controller", port, "sensor.identity");
  a.identity(bool(s.clock_));
  a.indexed_label("controller", port, "sensor.next", sizeof(s.next_));
  a.field(s.next_);
  a.indexed_label("controller", port, "sensor.start", sizeof(s.start_));
  a.field(s.start_);
  a.indexed_label("controller", port, "sensor.pulsing", sizeof(s.pulsing_));
  a.field(s.pulsing_);
  a.indexed_label("controller", port, "sensor.bpm", sizeof(s.bpm_));
  a.bounded(s.bpm_, 30u, 180u);
}

void CoreState::visit(state::Archive &a, TransferPak &p, unsigned port) {
  a.indexed_label("controller", port, "transfer.identity");
  a.identity(bool(p.cartridge_));
  a.indexed_label("controller", port, "transfer.bank", sizeof(p.bank_));
  a.bounded(p.bank_, 0u, 3u);
  a.indexed_label("controller", port, "transfer.reset", sizeof(p.reset_));
  a.bounded(p.reset_, 0u, 3u);
  a.indexed_label("controller", port, "transfer.enabled", sizeof(p.enabled_));
  a.field(p.enabled_);
  a.indexed_label("controller", port, "transfer.cartridge_enabled", sizeof(p.cartridge_enabled_));
  a.field(p.cartridge_enabled_);
  a.indexed_label("controller", port, "transfer.empty_board", sizeof(p.empty_board_));
  a.field(p.empty_board_);
  if (p.cartridge_) {
    auto *cartridge = dynamic_cast<HandheldCartridge *>(p.cartridge_.get());
    state::Archive::require(cartridge != nullptr);
    visit(a, *cartridge, port);
  }
}

void CoreState::visit(state::Archive &a, HandheldCartridge &cartridge, unsigned port) {
  auto &c = *cartridge.implementation_;
  a.indexed_label("controller", port, "transfer.cartridge.config.board", sizeof(c.config.board));
  a.identity(c.config.board);
  a.indexed_label("controller", port, "transfer.cartridge.config.ram_size",
                  sizeof(c.config.ram_size));
  a.identity(c.config.ram_size);
  a.indexed_label("controller", port, "transfer.cartridge.config.rtc", sizeof(c.config.rtc));
  a.identity(c.config.rtc);
  a.indexed_label("controller", port, "transfer.cartridge.config.eeprom_size",
                  sizeof(c.config.eeprom_size));
  a.identity(c.config.eeprom_size);
  a.indexed_label("controller", port, "transfer.cartridge.rom_size", sizeof(c.rom_size));
  a.identity(c.rom_size);
  a.indexed_label("controller", port, "transfer.cartridge.rom", 8);
  a.bytes_identity(c.rom);
  a.indexed_fixed_vector(c.ram, "controller", port, "transfer.cartridge.ram");
  a.indexed_fixed_vector(c.flash, "controller", port, "transfer.cartridge.flash");
  a.indexed_fixed_vector(c.eeprom, "controller", port, "transfer.cartridge.eeprom");
  a.indexed_label("controller", port, "transfer.cartridge.rom_bank", sizeof(c.rom_bank));
  a.field(c.rom_bank);
  a.indexed_label("controller", port, "transfer.cartridge.ram_bank", sizeof(c.ram_bank));
  a.field(c.ram_bank);
  a.indexed_label("controller", port, "transfer.cartridge.base_bank", sizeof(c.base_bank));
  a.field(c.base_bank);
  a.indexed_label("controller", port, "transfer.cartridge.mode", sizeof(c.mode));
  a.field(c.mode);
  a.indexed_label("controller", port, "transfer.cartridge.ram_enabled", sizeof(c.ram_enabled));
  a.field(c.ram_enabled);
  a.indexed_label("controller", port, "transfer.cartridge.extra_enabled", sizeof(c.extra_enabled));
  a.field(c.extra_enabled);
  a.indexed_label("controller", port, "transfer.cartridge.rumble", sizeof(c.rumble));
  a.field(c.rumble);
  a.indexed_label("controller", port, "transfer.cartridge.board_present", sizeof(c.board_present));
  a.field(c.board_present);
  a.indexed_label("controller", port, "transfer.cartridge.region_bank", sizeof(c.region_bank[0]));
  a.array(c.region_bank);
  a.indexed_label("controller", port, "transfer.cartridge.ram_region", sizeof(c.ram_region[0]));
  a.array(c.ram_region);
  a.indexed_label("controller", port, "transfer.cartridge.region_flash", sizeof(c.region_flash[0]));
  a.array(c.region_flash);
  a.indexed_label("controller", port, "transfer.cartridge.flash_enabled", sizeof(c.flash_enabled));
  a.field(c.flash_enabled);
  a.indexed_label("controller", port, "transfer.cartridge.flash_writable",
                  sizeof(c.flash_writable));
  a.field(c.flash_writable);
  a.indexed_label("controller", port, "transfer.cartridge.time", sizeof(c.time[0]));
  a.array(c.time);
  a.indexed_label("controller", port, "transfer.cartridge.latched", sizeof(c.latched[0]));
  a.array(c.latched);
  a.indexed_label("controller", port, "transfer.cartridge.latch", sizeof(c.latch));
  a.field(c.latch);
  a.indexed_label("controller", port, "transfer.cartridge.motion_x", sizeof(c.motion_x));
  a.field(c.motion_x);
  a.indexed_label("controller", port, "transfer.cartridge.motion_y", sizeof(c.motion_y));
  a.field(c.motion_y);
  a.indexed_label("controller", port, "transfer.cartridge.tilt_x", sizeof(c.tilt_x));
  a.field(c.tilt_x);
  a.indexed_label("controller", port, "transfer.cartridge.tilt_y", sizeof(c.tilt_y));
  a.field(c.tilt_y);
  a.indexed_label("controller", port, "transfer.cartridge.select", sizeof(c.select));
  a.field(c.select);
  a.indexed_label("controller", port, "transfer.cartridge.input", sizeof(c.input));
  a.field(c.input);
  a.indexed_label("controller", port, "transfer.cartridge.output", sizeof(c.output));
  a.field(c.output);
  a.indexed_label("controller", port, "transfer.cartridge.index", sizeof(c.index));
  a.field(c.index);
  a.indexed_label("controller", port, "transfer.cartridge.ready", sizeof(c.ready));
  a.field(c.ready);
  a.indexed_label("controller", port, "transfer.cartridge.time_index", sizeof(c.time_index));
  a.field(c.time_index);
  a.indexed_label("controller", port, "transfer.cartridge.calendar", sizeof(c.calendar[0]));
  a.array(c.calendar);
  a.indexed_label("controller", port, "transfer.cartridge.serial_select", sizeof(c.serial_select));
  a.field(c.serial_select);
  a.indexed_label("controller", port, "transfer.cartridge.serial_clock", sizeof(c.serial_clock));
  a.field(c.serial_clock);
  a.indexed_label("controller", port, "transfer.cartridge.serial_writable",
                  sizeof(c.serial_writable));
  a.field(c.serial_writable);
  a.indexed_label("controller", port, "transfer.cartridge.serial_input", sizeof(c.serial_input));
  a.field(c.serial_input);
  a.indexed_label("controller", port, "transfer.cartridge.serial_bits", sizeof(c.serial_bits));
  a.field(c.serial_bits);
  a.indexed_label("controller", port, "transfer.cartridge.serial_output", sizeof(c.serial_output));
  a.field(c.serial_output);
  a.indexed_label("controller", port, "transfer.cartridge.serial_remaining",
                  sizeof(c.serial_remaining));
  a.field(c.serial_remaining);
  a.indexed_label("controller", port, "transfer.cartridge.serial_address",
                  sizeof(c.serial_address));
  a.field(c.serial_address);
  a.indexed_label("controller", port, "transfer.cartridge.serial_busy", sizeof(c.serial_busy));
  a.field(c.serial_busy);
  a.indexed_label("controller", port, "transfer.cartridge.serial_mode", sizeof(c.serial_mode));
  a.field(c.serial_mode);
}

void CoreState::visit(state::Archive &a, Aleck64 &c) {
  a.label("arcade.profile", sizeof(c.profile_));
  a.identity(c.profile_);
  a.fixed_vector(c.sdram_, "arcade.sdram");
  a.label("arcade.video_ram", sizeof(c.video_ram_[0]));
  a.array(c.video_ram_);
  a.label("arcade.palette_ram", sizeof(c.palette_ram_[0]));
  a.array(c.palette_ram_);
  for (unsigned player = 0; player < c.input_.players.size(); ++player) {
    auto &p = c.input_.players[player];
    a.indexed_label("arcade.players", player, "up", sizeof(p.up));
    a.field(p.up);
    a.indexed_label("arcade.players", player, "down", sizeof(p.down));
    a.field(p.down);
    a.indexed_label("arcade.players", player, "left", sizeof(p.left));
    a.field(p.left);
    a.indexed_label("arcade.players", player, "right", sizeof(p.right));
    a.field(p.right);
    a.indexed_label("arcade.players", player, "start", sizeof(p.start));
    a.field(p.start);
    a.indexed_label("arcade.players", player, "coin", sizeof(p.coin));
    a.field(p.coin);
    a.indexed_label("arcade.players", player, "x", sizeof(p.x));
    a.field(p.x);
    a.indexed_label("arcade.players", player, "y", sizeof(p.y));
    a.field(p.y);
    a.indexed_label("arcade.players", player, "buttons", sizeof(p.buttons[0]));
    a.array(p.buttons);
  }
  a.label("arcade.input.service", sizeof(c.input_.service));
  a.field(c.input_.service);
  a.label("arcade.input.test", sizeof(c.input_.test));
  a.field(c.input_.test);
  a.label("arcade.input.mahjong", sizeof(c.input_.mahjong));
  a.field(c.input_.mahjong);
  a.label("arcade.dip_switches", sizeof(c.dip_switches_[0]));
  a.array(c.dip_switches_);
  a.label("arcade.mahjong_row", sizeof(c.mahjong_row_));
  a.field(c.mahjong_row_);
  a.label("arcade.video_enabled", sizeof(c.video_enabled_));
  a.field(c.video_enabled_);
}

} // namespace cupid::n64
