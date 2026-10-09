#include "fixture.hpp"

int main() {
  test::mi_tests();
  test::ri_tests();
  test::rdram_tests();
  test::instruction_tracking_tests();
  test::pi_tests();
  test::sram_tests();
  test::flash_tests();
  test::cartridge_bus_tests();
  test::isviewer_tests();
  test::arcade_memory_tests();
  test::arcade_control_tests();
  test::arcade_native_tests();
  test::cartridge_profile_tests();
  test::eeprom_reset_tests();
  test::rtc_tests();
  test::rtc_timing_tests();
  test::timing_tests();
  test::timer_dispatch_tests();
  test::cached_delay_slot_tests();
  test::cic_tests();
  test::pif_tests();
  test::gamepad_tests();
  test::accessory_tests();
  test::handheld_tests();
  test::disk_drive_tests();
  test::disk_clock_tests();
  test::disk_image_tests();
  test::rsp_tests();
  test::video_tests();
  test::audio_tests();
  test::rdp_tests();
  test::console_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
