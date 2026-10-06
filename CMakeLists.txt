cmake_minimum_required(VERSION 3.25)
project(cupid_n64 VERSION 0.1.0 LANGUAGES CXX)

add_library(cupid_core
  src/core/cpu/cpu.cpp
  src/core/cpu/integer.cpp
  src/core/cpu/memory.cpp
  src/core/cpu/control.cpp
  src/core/cpu/cache.cpp
  src/core/cpu/fpu/registers.cpp
  src/core/cpu/fpu/execute.cpp
  src/core/devices/mi/mips_interface.cpp
  src/core/devices/ri/ram_interface.cpp
  src/core/devices/rdram/rdram.cpp
)
target_include_directories(cupid_core PUBLIC src)
target_compile_features(cupid_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(cupid_core PRIVATE /W4 /WX /permissive-)
  set_source_files_properties(src/core/cpu/fpu/execute.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  target_compile_options(cupid_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_source_files_properties(src/core/cpu/fpu/execute.cpp PROPERTIES
    COMPILE_OPTIONS "-frounding-math;-ffp-contract=off")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(cpu_tests
    tests/cpu/main.cpp
    tests/cpu/integer.cpp
    tests/cpu/memory.cpp
    tests/cpu/control.cpp
    tests/cpu/cache.cpp
    tests/cpu/fpu.cpp
  )
  target_link_libraries(cpu_tests PRIVATE cupid_core)
  add_test(NAME vr4300 COMMAND cpu_tests)
  add_executable(device_tests
    tests/devices/main.cpp
    tests/devices/mi.cpp
    tests/devices/ri.cpp
    tests/devices/rdram.cpp
  )
  target_link_libraries(device_tests PRIVATE cupid_core)
  add_test(NAME console_devices COMMAND device_tests)
endif()
