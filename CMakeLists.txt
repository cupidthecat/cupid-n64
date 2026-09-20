cmake_minimum_required(VERSION 3.22)
project(CupidN64 VERSION 0.1.0 LANGUAGES CXX)

option(CUPID_STRICT "Treat compiler warnings as errors" ON)
option(CUPID_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
set(CUPID_TEST_ROM "" CACHE FILEPATH "Path to the complete N64 test ROM")
set(CUPID_EXTENDED_TEST_ROM "" CACHE FILEPATH "Path to the N64 test ROM with optional accuracy groups")
set(CUPID_PIF_ROM "" CACHE FILEPATH "Path to the PIF boot ROM for cartridge tests")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB CUPID_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB CUPID_CPU_SOURCES CONFIGURE_DEPENDS "src/cpu/*.cpp")
file(GLOB CUPID_RCP_SOURCES CONFIGURE_DEPENDS "src/rcp/*.cpp")
file(GLOB CUPID_BUS_SOURCES CONFIGURE_DEPENDS "src/bus/*.cpp")
file(GLOB CUPID_CARTRIDGE_SOURCES CONFIGURE_DEPENDS "src/cartridge/*.cpp")
file(GLOB CUPID_RDP_SOURCES CONFIGURE_DEPENDS "src/rdp/*.cpp")
file(GLOB CUPID_VI_SOURCES CONFIGURE_DEPENDS "src/vi/*.cpp")
list(APPEND CUPID_CORE_SOURCES ${CUPID_CPU_SOURCES} ${CUPID_RCP_SOURCES} ${CUPID_BUS_SOURCES} ${CUPID_CARTRIDGE_SOURCES} ${CUPID_RDP_SOURCES} ${CUPID_VI_SOURCES})
list(FILTER CUPID_CORE_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(cupid_core STATIC ${CUPID_CORE_SOURCES})
target_include_directories(cupid_core PUBLIC include)

file(GLOB CUPID_HOST_SOURCES CONFIGURE_DEPENDS "src/host/*.cpp" "src/storage/*.cpp")
add_library(cupid_host STATIC ${CUPID_HOST_SOURCES})
target_include_directories(cupid_host PUBLIC include)
target_link_libraries(cupid_host PUBLIC cupid_core)

add_executable(cupid-n64 src/main.cpp)
target_link_libraries(cupid-n64 PRIVATE cupid_host)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(cupid-n64 PRIVATE -municode)
endif()

enable_testing()
file(GLOB CUPID_TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.cpp")
file(GLOB CUPID_RCP_TEST_SOURCES CONFIGURE_DEPENDS "tests/rcp/test_*.cpp" "tests/cpu/test_*.cpp" "tests/rdp/test_*.cpp" "tests/vi/test_*.cpp" "tests/rsp/test_*.cpp")
file(GLOB CUPID_CARTRIDGE_TEST_SOURCES CONFIGURE_DEPENDS "tests/cartridge/test_*.cpp" "tests/host/test_*.cpp" "tests/storage/test_*.cpp")
list(APPEND CUPID_TEST_SOURCES ${CUPID_RCP_TEST_SOURCES} ${CUPID_CARTRIDGE_TEST_SOURCES})
add_executable(cupid-tests tests/main.cpp ${CUPID_TEST_SOURCES})
target_include_directories(cupid-tests PRIVATE tests)
target_link_libraries(cupid-tests PRIVATE cupid_host)
add_test(NAME hardware COMMAND cupid-tests)

add_executable(cupid-warm-reset-tests tests/boot/warm_reset.cpp)
target_link_libraries(cupid-warm-reset-tests PRIVATE cupid_core)

add_executable(cupid-storage-writer tests/host/storage_writer.cpp)
target_link_libraries(cupid-storage-writer PRIVATE cupid_host)
add_executable(cupid-compatibility-capture tests/compatibility/capture.cpp)
target_link_libraries(cupid-compatibility-capture PRIVATE cupid_host)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(cupid-compatibility-capture PRIVATE -municode)
endif()
add_test(NAME host_storage_concurrent
    COMMAND "${CMAKE_COMMAND}"
        -DWRITER=$<TARGET_FILE:cupid-storage-writer>
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/host-storage-concurrent
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/concurrent_storage.cmake)
set_tests_properties(host_storage_concurrent PROPERTIES LABELS "host")

foreach(target cupid_core cupid_host cupid-n64 cupid-tests cupid-warm-reset-tests cupid-storage-writer cupid-compatibility-capture)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:strict)
        if(CUPID_STRICT)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -fno-fast-math -frounding-math)
        if(CUPID_STRICT)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(CUPID_SANITIZERS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        else()
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endforeach()

if(CUPID_SANITIZERS AND WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-target-triple
        OUTPUT_VARIABLE CUPID_COMPILER_TRIPLE OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX REPLACE "-.*" "" CUPID_SANITIZER_ARCH "${CUPID_COMPILER_TRIPLE}")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "--print-file-name=clang_rt.asan_dynamic-${CUPID_SANITIZER_ARCH}.dll"
        OUTPUT_VARIABLE CUPID_SANITIZER_DLL OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT EXISTS "${CUPID_SANITIZER_DLL}")
        message(FATAL_ERROR "The compiler's sanitizer runtime was not found: ${CUPID_SANITIZER_DLL}")
    endif()
    foreach(target cupid-n64 cupid-tests cupid-warm-reset-tests cupid-storage-writer cupid-compatibility-capture)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CUPID_SANITIZER_DLL}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
endif()

function(cupid_add_rom_test name image)
    if(NOT EXISTS "${image}")
        message(FATAL_ERROR "Test ROM does not exist: ${image}")
    endif()
    if(NOT EXISTS "${CUPID_PIF_ROM}")
        message(FATAL_ERROR "Cartridge tests require CUPID_PIF_ROM to name a PIF boot ROM")
    endif()
    set(report_options --save none --require-test-success)
    if(name STREQUAL "nemu64_extended")
        list(APPEND report_options --require-extended-tests)
    endif()
    add_test(NAME ${name} COMMAND cupid-n64 "${image}" --pif "${CUPID_PIF_ROM}" --max-instructions 4000000000 ${report_options})
    set_tests_properties(${name} PROPERTIES TIMEOUT 1800 LABELS "accuracy")
endfunction()

if(CUPID_TEST_ROM)
    cupid_add_rom_test(nemu64_complete "${CUPID_TEST_ROM}")
    add_test(NAME nemu64_warm_reset COMMAND cupid-warm-reset-tests "${CUPID_TEST_ROM}" "${CUPID_PIF_ROM}")
    set_tests_properties(nemu64_warm_reset PROPERTIES TIMEOUT 1800 LABELS "accuracy;boot")
    if(CUPID_PIF_ROM)
        add_test(NAME compatibility_capture
            COMMAND "${CMAKE_COMMAND}"
                -DCAPTURE=$<TARGET_FILE:cupid-compatibility-capture>
                -DROM=${CUPID_TEST_ROM}
                -DPIF=${CUPID_PIF_ROM}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-capture
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/capture.cmake)
        set_tests_properties(compatibility_capture PROPERTIES TIMEOUT 180 LABELS "compatibility")
        add_test(NAME host_storage_runner
            COMMAND "${CMAKE_COMMAND}"
                -DRUNNER=$<TARGET_FILE:cupid-n64>
                -DROM=${CUPID_TEST_ROM}
                -DPIF=${CUPID_PIF_ROM}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/host-storage-runner
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/runner_storage.cmake)
        set_tests_properties(host_storage_runner PROPERTIES LABELS "host")
    endif()
endif()
if(CUPID_EXTENDED_TEST_ROM)
    cupid_add_rom_test(nemu64_extended "${CUPID_EXTENDED_TEST_ROM}")
endif()
