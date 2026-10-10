cmake_minimum_required(VERSION 3.25)
project(cupid_n64 VERSION 0.1.0 LANGUAGES C CXX)
include(cmake/Optimization.cmake)

add_library(cupid_core
  src/core/arcade/aleck64.cpp
  src/core/arcade/memory.cpp
  src/core/arcade/controls.cpp
  src/core/cpu/cpu.cpp
  src/core/cpu/execution.cpp
  src/core/cpu/execution/memory.cpp
  src/core/cpu/recompiler/cache.cpp
  src/core/cpu/recompiler/clocks.cpp
  src/core/cpu/recompiler/pipeline.cpp
  src/core/cpu/recompiler/branch.cpp
  src/core/cpu/recompiler/emitter.cpp
  src/core/cpu/recompiler/integer.cpp
  src/core/cpu/recompiler/arithmetic.cpp
  src/core/cpu/recompiler/control/traps.cpp
  src/core/cpu/recompiler/control/noops.cpp
  src/core/cpu/recompiler/memory.cpp
  src/core/cpu/recompiler/memory/merge.cpp
  src/core/cpu/recompiler/fpu/execute.cpp
  src/core/cpu/recompiler/fpu/registers.cpp
  src/core/cpu/recompiler/fpu/arithmetic.cpp
  src/core/cpu/recompiler/fpu/convert.cpp
  src/core/cpu/recompiler/fpu/compare.cpp
  src/core/cpu/recompiler/fpu/host.cpp
  src/core/cpu/integer.cpp
  src/core/cpu/memory.cpp
  src/core/cpu/memory/tlb.cpp
  src/core/cpu/memory/linked.cpp
  src/core/cpu/memory/merge_store.cpp
  src/core/cpu/control.cpp
  src/core/cpu/control/cop2.cpp
  src/core/cpu/control/timer.cpp
  src/core/cpu/cache.cpp
  src/core/cpu/fpu/registers.cpp
  src/core/cpu/fpu/execute.cpp
  src/core/devices/mi/mips_interface.cpp
  src/core/devices/ri/ram_interface.cpp
  src/core/devices/rdram/rdram.cpp
  src/core/devices/pi/peripheral_interface.cpp
  src/core/devices/pi/dma.cpp
  src/core/devices/cic/cic.cpp
  src/core/devices/joybus/crc.cpp
  src/core/devices/pif/pif.cpp
  src/core/devices/pif/joybus.cpp
  src/core/devices/si/serial_interface.cpp
  src/core/cartridge/eeprom.cpp
  src/core/cartridge/joybus.cpp
  src/core/cartridge/rtc/rtc.cpp
  src/core/cartridge/rtc/calendar.cpp
  src/core/cartridge/rtc/joybus.cpp
  src/core/cartridge/sram.cpp
  src/core/cartridge/isviewer/isviewer.cpp
  src/core/cartridge/flash/flash.cpp
  src/core/cartridge/flash/io.cpp
  src/core/cartridge/flash/commands.cpp
  src/core/controller/gamepad.cpp
  src/core/controller/gamecube/gamecube.cpp
  src/core/controller/gamecube/report.cpp
  src/core/controller/stick/response.cpp
  src/core/controller/pak.cpp
  src/core/controller/mouse/mouse.cpp
  src/core/controller/accessories/transfer_pak.cpp
  src/core/controller/accessories/bio_sensor.cpp
  src/core/controller/accessories/cartridge/handheld.cpp
  src/core/controller/accessories/cartridge/identity.cpp
  src/core/controller/accessories/cartridge/boards.cpp
  src/core/controller/accessories/cartridge/clock.cpp
  src/core/controller/accessories/cartridge/tama.cpp
  src/core/controller/accessories/cartridge/serial.cpp
  src/core/rsp/rsp.cpp
  src/core/rsp/recompiler/cache.cpp
  src/core/rsp/recompiler/emitter.cpp
  src/core/rsp/recompiler/branch.cpp
  src/core/rsp/recompiler/pipeline.cpp
  src/core/rsp/recompiler/integer.cpp
  src/core/rsp/recompiler/memory.cpp
  src/core/rsp/recompiler/vector/memory.cpp
  src/core/rsp/recompiler/vector/arithmetic.cpp
  src/core/rsp/recompiler/vector/linear.cpp
  src/core/rsp/recompiler/vector/quad.cpp
  src/core/rsp/recompiler/vector/packed.cpp
  src/core/rsp/recompiler/vector/transpose.cpp
  src/core/rsp/recompiler/vector/simd.cpp
  src/core/rsp/io.cpp
  src/core/rsp/dma.cpp
  src/core/rsp/scalar/execute.cpp
  src/core/rsp/pipeline/decode.cpp
  src/core/rsp/pipeline/execute.cpp
  src/core/rsp/vector/transfer.cpp
  src/core/rsp/vector/memory.cpp
  src/core/rsp/vector/execute.cpp
  src/core/rsp/vector/scalar.cpp
  src/core/rsp/vector/simd.cpp
  src/core/rsp/vector/simd/sse.cpp
  src/core/rsp/vector/divide.cpp
  src/core/devices/vi/video_interface.cpp
  src/core/devices/vi/io.cpp
  src/core/devices/vi/scanout.cpp
  src/core/devices/audio/audio_interface.cpp
  src/core/devices/audio/io.cpp
  src/core/rdp/rdp.cpp
  src/core/rdp/io.cpp
  src/core/rdp/commands.cpp
  src/core/cartridge/rom.cpp
  src/core/cartridge/profile/boot.cpp
  src/core/cartridge/profile/profile.cpp
  src/core/system/console.cpp
  src/core/disk/drive.cpp
  src/core/disk/controller/commands.cpp
  src/core/disk/controller/transfers.cpp
  src/core/disk/controller/io.cpp
  src/core/disk/clock/clock.cpp
  src/core/disk/image/image.cpp
  src/core/disk/image/validation.cpp
  src/core/disk/image/mapping.cpp
  src/core/system/bus.cpp
)
target_include_directories(cupid_core PUBLIC src)
target_compile_features(cupid_core PUBLIC cxx_std_20)
include(cmake/Compiler.cmake)
if(MSVC)
  target_compile_options(cupid_core PRIVATE /W4 /WX /permissive-)
  set_source_files_properties(src/core/cpu/fpu/execute.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  target_compile_options(cupid_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_source_files_properties(src/core/cpu/fpu/execute.cpp PROPERTIES
    COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
    set_source_files_properties(src/core/rsp/vector/simd/sse.cpp PROPERTIES COMPILE_OPTIONS -msse4.1)
  endif()
endif()

option(CUPID_HARDWARE_RENDERER "Build Vulkan rendering" ON)
option(CUPID_REQUIRE_HARDWARE_RENDERER_TESTS "Fail renderer tests when Vulkan is unavailable" OFF)
if(CUPID_HARDWARE_RENDERER)
  include(cmake/Renderer.cmake)
endif()

option(CUPID_DESKTOP "Build the Windows desktop frontend" ON)
add_library(cupid_video_output src/desktop/video/output.cpp)
target_link_libraries(cupid_video_output PUBLIC cupid_core)
if(CUPID_HARDWARE_RENDERER)
  target_compile_definitions(cupid_video_output PRIVATE CUPID_HARDWARE_RENDERER)
  target_link_libraries(cupid_video_output PRIVATE cupid_renderer)
endif()
if(MSVC)
  target_compile_options(cupid_video_output PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(cupid_video_output PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32 AND CUPID_DESKTOP)
  add_executable(cupid_desktop WIN32
    src/desktop/windows/main.cpp
    src/desktop/windows/window.cpp
    src/desktop/windows/audio.cpp
    src/desktop/windows/video/paint_buffer.cpp
    src/desktop/audio/resampler.cpp
    src/desktop/session.cpp
    src/desktop/storage/save.cpp
    src/desktop/timing/playback_clock.cpp
  )
  set_target_properties(cupid_desktop PROPERTIES OUTPUT_NAME cupid-n64)
  target_compile_definitions(cupid_desktop PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(cupid_desktop PRIVATE cupid_video_output comdlg32 shell32 winmm gdi32)
  if(MSVC)
    target_compile_options(cupid_desktop PRIVATE /W4 /WX /permissive-)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(video_output_tests tests/desktop/video_output.cpp)
  target_link_libraries(video_output_tests PRIVATE cupid_video_output)
  add_test(NAME cpu_video_output COMMAND video_output_tests)
  add_test(NAME cpu_video_output_unavailable COMMAND video_output_tests --unavailable)
  set_tests_properties(cpu_video_output_unavailable PROPERTIES ENVIRONMENT
    "VK_DRIVER_FILES=${CMAKE_CURRENT_BINARY_DIR}/missing-vulkan-driver.json;VK_ICD_FILENAMES=${CMAKE_CURRENT_BINARY_DIR}/missing-vulkan-driver.json")
  add_executable(desktop_resampler_tests tests/desktop/resampler.cpp src/desktop/audio/resampler.cpp)
  target_include_directories(desktop_resampler_tests PRIVATE src)
  target_compile_features(desktop_resampler_tests PRIVATE cxx_std_20)
  add_test(NAME desktop_audio_resampling COMMAND desktop_resampler_tests)
  add_executable(desktop_storage_tests tests/desktop/storage.cpp src/desktop/storage/save.cpp)
  target_include_directories(desktop_storage_tests PRIVATE src)
  target_compile_features(desktop_storage_tests PRIVATE cxx_std_20)
  target_compile_definitions(desktop_storage_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  add_test(NAME desktop_save_storage COMMAND desktop_storage_tests)
  add_executable(desktop_clock_tests
    tests/desktop/playback_clock.cpp
    src/desktop/timing/playback_clock.cpp
  )
  target_include_directories(desktop_clock_tests PRIVATE src)
  target_compile_features(desktop_clock_tests PRIVATE cxx_std_20)
  add_test(NAME desktop_playback_clock COMMAND desktop_clock_tests)
  if(TARGET cupid_desktop)
    add_executable(session_video_tests tests/desktop/session_video.cpp
      src/desktop/session.cpp src/desktop/storage/save.cpp
      src/desktop/windows/audio.cpp src/desktop/audio/resampler.cpp)
    target_compile_definitions(session_video_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(session_video_tests PRIVATE cupid_video_output winmm)
    add_test(NAME desktop_software_video COMMAND session_video_tests)
    add_test(NAME desktop_software_video_unavailable COMMAND session_video_tests --unavailable)
    set_tests_properties(desktop_software_video_unavailable PROPERTIES ENVIRONMENT
      "VK_DRIVER_FILES=${CMAKE_CURRENT_BINARY_DIR}/missing-vulkan-driver.json;VK_ICD_FILENAMES=${CMAKE_CURRENT_BINARY_DIR}/missing-vulkan-driver.json")
    add_executable(desktop_audio_tests tests/desktop/audio_device.cpp)
    target_include_directories(desktop_audio_tests PRIVATE src)
    target_compile_features(desktop_audio_tests PRIVATE cxx_std_20)
    target_compile_definitions(desktop_audio_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME desktop_audio_devices COMMAND desktop_audio_tests)
  endif()
  if(WIN32)
    add_executable(desktop_video_tests
      tests/desktop/paint_buffer.cpp
      src/desktop/windows/video/paint_buffer.cpp
    )
    target_include_directories(desktop_video_tests PRIVATE src)
    target_compile_features(desktop_video_tests PRIVATE cxx_std_20)
    target_compile_definitions(desktop_video_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(desktop_video_tests PRIVATE gdi32)
    add_test(NAME desktop_frame_presentation COMMAND desktop_video_tests)
  endif()
  add_executable(cpu_tests
    tests/cpu/main.cpp
    tests/cpu/integer.cpp
    tests/cpu/memory.cpp
    tests/cpu/memory/tlb_lookup.cpp
    tests/cpu/memory/frozen_cache.cpp
    tests/cpu/memory/addressing.cpp
    tests/cpu/linked.cpp
    tests/cpu/merge_store.cpp
    tests/cpu/control.cpp
    tests/cpu/control/timer.cpp
    tests/cpu/control/dispatch.cpp
    tests/cpu/control/cop2_memory.cpp
    tests/cpu/control/cop2_memory_modes.cpp
    tests/cpu/control/nmi.cpp
    tests/cpu/random.cpp
    tests/cpu/cache.cpp
    tests/cpu/cache_coherence.cpp
    tests/cpu/cache_refill.cpp
    tests/cpu/execution.cpp
    tests/cpu/native_memory.cpp
    tests/cpu/native/timing.cpp
    tests/cpu/native/arithmetic.cpp
    tests/cpu/native/merge_linked.cpp
    tests/cpu/native/control_traps.cpp
    tests/cpu/native/control_noops.cpp
    tests/cpu/native/cop2.cpp
    tests/cpu/native/byte_order.cpp
    tests/cpu/native/code_write.cpp
    tests/cpu/native_fpu_memory.cpp
    tests/cpu/native_fpu.cpp
    tests/cpu/native/fpu/timing.cpp
    tests/cpu/native/fpu/conditional.cpp
    tests/cpu/native/interrupt_entry.cpp
    tests/cpu/native_branches.cpp
    tests/cpu/native_entries.cpp
    tests/cpu/section_invalidation.cpp
    tests/cpu/native_loops.cpp
    tests/cpu/native_boundaries.cpp
    tests/cpu/fpu.cpp
    tests/cpu/replay/decode/replay.cpp
    tests/cpu/replay/cached/replay.cpp
    tests/cpu/replay/memory/replay.cpp
    tests/cpu/replay/linked/replay.cpp
    tests/cpu/replay/priority/replay.cpp
    tests/cpu/replay/endian/replay.cpp
    tests/cpu/replay/endian/linked/replay.cpp
    tests/cpu/replay/cop2_memory/replay.cpp
    tests/cpu/replay/cache/maintenance/replay.cpp
    tests/cpu/replay/translation/replay.cpp
    tests/cpu/replay/fpu/numeric/replay.cpp
  )
  target_link_libraries(cpu_tests PRIVATE cupid_core)
  if(MSVC)
    target_link_options(cpu_tests PRIVATE /STACK:8388608)
  endif()
  add_test(NAME vr4300 COMMAND cpu_tests)
  add_executable(device_tests
    tests/devices/main.cpp
    tests/devices/mi.cpp
    tests/devices/ri.cpp
    tests/devices/rdram.cpp
    tests/devices/memory/word_pairs.cpp
    tests/devices/memory/bus_boundaries.cpp
    tests/devices/instruction_tracking.cpp
    tests/devices/pi.cpp
    tests/devices/pi/boundaries/replay.cpp
    tests/devices/cartridge/sram.cpp
    tests/devices/cartridge/flash.cpp
    tests/devices/cartridge/integration.cpp
    tests/devices/cartridge/isviewer.cpp
    tests/devices/arcade/memory.cpp
    tests/devices/arcade/controls.cpp
    tests/devices/arcade/native.cpp
    tests/devices/cartridge/profile.cpp
    tests/devices/cartridge/eeprom_reset.cpp
    tests/devices/cartridge/rtc.cpp
    tests/devices/cartridge/rtc_timing.cpp
    tests/devices/timing.cpp
    tests/devices/system/timer_dispatch.cpp
    tests/devices/system/cached_delay_slots.cpp
    tests/devices/system/entropy.cpp
    tests/devices/system/reset/immediate/replay.cpp
    tests/devices/system/reset/native/replay.cpp
    tests/devices/system/events/replay.cpp
    tests/devices/cic.cpp
    tests/devices/pif.cpp
    tests/devices/pif/boot/replay.cpp
    tests/devices/si/boundaries/replay.cpp
    tests/devices/pif/cartridge_status.cpp
    tests/devices/gamepad.cpp
    tests/devices/controller/stick.cpp
    tests/devices/controller/gamecube/commands.cpp
    tests/devices/controller/gamecube/pif.cpp
    tests/devices/controller/gamecube/replay.cpp
    tests/devices/controller/accessories.cpp
    tests/devices/controller/handheld.cpp
    tests/devices/disk/drive.cpp
    tests/devices/disk/boundaries/fixture.cpp
    tests/devices/disk/boundaries/commands.cpp
    tests/devices/disk/boundaries/transfers.cpp
    tests/devices/disk/boundaries/scheduler.cpp
    tests/devices/disk/clock.cpp
    tests/devices/disk/image.cpp
    tests/devices/rsp.cpp
    tests/devices/video.cpp
    tests/devices/video/scanout.cpp
    tests/devices/audio.cpp
    tests/devices/audio/boundaries/replay.cpp
    tests/devices/rdp.cpp
    tests/devices/console.cpp
  )
  target_link_libraries(device_tests PRIVATE cupid_core)
  add_test(NAME console_devices COMMAND device_tests)
  add_executable(rsp_tests
    tests/rsp/main.cpp
    tests/rsp/scalar.cpp
    tests/rsp/vector.cpp
    tests/rsp/simd.cpp
    tests/rsp/native.cpp
    tests/rsp/native_memory.cpp
    tests/rsp/native_vector.cpp
    tests/rsp/blocks.cpp
    tests/rsp/recompiler/context.cpp
    tests/rsp/recompiler/halt.cpp
    tests/rsp/memory.cpp
    tests/rsp/pipeline.cpp
    tests/rsp/dma/replay.cpp
    tests/rsp/replay/arithmetic/replay.cpp
    tests/rsp/replay/memory/replay.cpp
    tests/rsp/replay/blocks/replay.cpp
    tests/rsp/replay/memory_blocks/replay.cpp
    tests/rsp/replay/pipeline/replay.cpp
    tests/rsp/replay/nested/replay.cpp
  )
  target_link_libraries(rsp_tests PRIVATE cupid_core)
  add_test(NAME signal_processor COMMAND rsp_tests)
  if(CUPID_HARDWARE_RENDERER)
    add_executable(renderer_readback_tests tests/renderer/readback.cpp)
    target_link_libraries(renderer_readback_tests PRIVATE cupid_renderer)
    add_test(NAME frame_readback COMMAND renderer_readback_tests)
    set_tests_properties(frame_readback PROPERTIES TIMEOUT 15)
    add_executable(renderer_tests tests/renderer/main.cpp tests/renderer/cache_visibility.cpp
      tests/renderer/cache/depth.cpp tests/renderer/cache/overlap.cpp
      tests/renderer/replay/render.cpp tests/renderer/replay/reset.cpp
      tests/renderer/replay/scanout.cpp)
    target_link_libraries(renderer_tests PRIVATE cupid_renderer)
    add_test(NAME hardware_renderer COMMAND renderer_tests)
    if(NOT CUPID_REQUIRE_HARDWARE_RENDERER_TESTS)
      set_tests_properties(hardware_renderer PROPERTIES SKIP_RETURN_CODE 77)
    endif()
  endif()
endif()
