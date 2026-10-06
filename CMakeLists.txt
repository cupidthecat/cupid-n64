cmake_minimum_required(VERSION 3.25)
project(cupid_n64 VERSION 0.1.0 LANGUAGES C CXX)

add_library(cupid_core
  src/core/cpu/cpu.cpp
  src/core/cpu/execution.cpp
  src/core/cpu/recompiler/cache.cpp
  src/core/cpu/recompiler/emitter.cpp
  src/core/cpu/recompiler/integer.cpp
  src/core/cpu/integer.cpp
  src/core/cpu/memory.cpp
  src/core/cpu/control.cpp
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
  src/core/controller/gamepad.cpp
  src/core/controller/pak.cpp
  src/core/rsp/rsp.cpp
  src/core/rsp/recompiler/cache.cpp
  src/core/rsp/recompiler/emitter.cpp
  src/core/rsp/recompiler/integer.cpp
  src/core/rsp/recompiler/memory.cpp
  src/core/rsp/io.cpp
  src/core/rsp/dma.cpp
  src/core/rsp/scalar/execute.cpp
  src/core/rsp/pipeline/decode.cpp
  src/core/rsp/pipeline/execute.cpp
  src/core/rsp/vector/transfer.cpp
  src/core/rsp/vector/memory.cpp
  src/core/rsp/vector/execute.cpp
  src/core/rsp/vector/simd.cpp
  src/core/rsp/vector/simd/sse.cpp
  src/core/rsp/vector/divide.cpp
  src/core/devices/vi/video_interface.cpp
  src/core/devices/vi/io.cpp
  src/core/devices/audio/audio_interface.cpp
  src/core/devices/audio/io.cpp
  src/core/rdp/rdp.cpp
  src/core/rdp/io.cpp
  src/core/rdp/commands.cpp
  src/core/cartridge/rom.cpp
  src/core/system/console.cpp
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
if(CUPID_HARDWARE_RENDERER)
  include(cmake/Renderer.cmake)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(cpu_tests
    tests/cpu/main.cpp
    tests/cpu/integer.cpp
    tests/cpu/memory.cpp
    tests/cpu/control.cpp
    tests/cpu/cache.cpp
    tests/cpu/execution.cpp
    tests/cpu/fpu.cpp
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
    tests/devices/pi.cpp
    tests/devices/timing.cpp
    tests/devices/cic.cpp
    tests/devices/pif.cpp
    tests/devices/gamepad.cpp
    tests/devices/rsp.cpp
    tests/devices/video.cpp
    tests/devices/audio.cpp
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
    tests/rsp/memory.cpp
    tests/rsp/pipeline.cpp
  )
  target_link_libraries(rsp_tests PRIVATE cupid_core)
  add_test(NAME signal_processor COMMAND rsp_tests)
  if(CUPID_HARDWARE_RENDERER)
    add_executable(renderer_tests tests/renderer/main.cpp)
    target_link_libraries(renderer_tests PRIVATE cupid_renderer)
    add_test(NAME hardware_renderer COMMAND renderer_tests)
    set_tests_properties(hardware_renderer PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()
