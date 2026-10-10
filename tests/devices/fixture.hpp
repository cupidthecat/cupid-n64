#pragma once

#include "../support/test.hpp"
#include "core/devices/mi/mips_interface.hpp"

namespace test {

struct MemoryFixture : cupid::n64::Bus {
  cupid::n64::RandomGenerator random;
  cupid::n64::RamInterface ri;
  cupid::n64::Rdram ram{ri, random};
  cupid::n64::MipsInterface mi{ram};
  cupid::n64::Cpu cpu{*this};

  explicit MemoryFixture(bool expansion = true) : ram(ri, random, expansion) {
    mi.connect([this](bool line) { cpu.set_interrupt(2, line); });
    cpu.write_control(cupid::n64::Status, 0x30000000);
    cpu.set_pc(0xffffffffa0001000);
  }

  cupid::n64::BusRead read(std::uint32_t address, unsigned bytes) override {
    return mi.read_rdram(address, bytes);
  }
  cupid::n64::BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    return mi.write_rdram(address, bytes, value);
  }
  cupid::n64::BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words) override {
    return mi.read_burst(address, words);
  }
  cupid::n64::BusWrite write_burst(std::uint32_t address,
                                   std::span<const std::uint32_t> words) override {
    return mi.write_burst(address, words);
  }

  void initialize() {
    ri.write_word(8, 0);
    ri.write_word(12, 0x14);
    mi.write_word(0, 0x10f);
    mi.write_rdram(0x03f80008, 4, 0x00080008);
    for (unsigned chip = 0; chip < ram.size() / 0x200000; ++chip) {
      ram.write_word(0x03f0000c, 0x02000000);
      ram.write_word(0x03f00004, ((ram.size() / 0x200000 + chip) * 2) << 26);
    }
    for (unsigned chip = 0; chip < ram.size() / 0x200000; ++chip)
      ram.write_word(0x03f00004 + ((ram.size() / 0x200000 + chip) * 2) * 0x400, chip * 2 << 26);
  }
};

void mi_tests();
void ri_tests();
void rdram_tests();
void memory_word_pair_tests();
void memory_bus_tests();
void instruction_tracking_tests();
void pi_tests();
void pi_boundary_tests();
void sram_tests();
void flash_tests();
void flash_boundary_tests();
void cartridge_bus_tests();
void isviewer_tests();
void arcade_memory_tests();
void arcade_control_tests();
void arcade_native_tests();
void cartridge_profile_tests();
void eeprom_reset_tests();
void rtc_tests();
void rtc_timing_tests();
void timing_tests();
void timer_dispatch_tests();
void cached_delay_slot_tests();
void entropy_tests();
void cic_tests();
void pif_tests();
void pif_boot_tests();
void si_boundary_tests();
void cartridge_status_tests();
void gamepad_tests();
void peripheral_bus_tests();
void peripheral_wire_tests();
void peripheral_input_tests();
void bio_clock_tests();
void pak_bank_tests();
void transfer_board_tests();
void transfer_special_tests();
void stick_tests();
void gamecube_tests();
void gamecube_route_tests();
void gamecube_replay_tests();
void accessory_tests();
void handheld_tests();
void disk_drive_tests();
void disk_boundary_tests();
void disk_clock_tests();
void disk_image_tests();
void rsp_tests();
void video_tests();
void software_video_tests();
void audio_tests();
void audio_boundary_tests();
void rdp_tests();
void console_tests();
void machine_reset_tests();
void reset_reuse_tests();
void cpu_event_tests();

} // namespace test
