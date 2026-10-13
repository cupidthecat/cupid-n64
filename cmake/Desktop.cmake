if(NOT CUPID_DESKTOP)
  return()
endif()

if(NOT WIN32)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The desktop frontend supports Windows and Linux")
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(cupid_gtk3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(cupid_sdl2 REQUIRED IMPORTED_TARGET sdl2>=2.0.18)
endif()

add_library(cupid_session
  ${PROJECT_SOURCE_DIR}/src/desktop/audio/audio.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/audio/resampler.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/audio/levels.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/session.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/session/disk.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/session/controllers.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/session/inspection.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/controls/ports.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/controls/mapping.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/controls/shortcuts/bindings.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/launch/files.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/state/session_state.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/state/slots.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/state/rewind.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/storage/recent.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/video/viewport.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/video/bitmap.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/timing/playback_clock.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/timing/execution_worker.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/replay/input.cpp
  ${PROJECT_SOURCE_DIR}/src/desktop/replay/field_trace.cpp
)
target_link_libraries(cupid_session PUBLIC cupid_video_output cupid_storage)
target_link_libraries(cupid_session PRIVATE Threads::Threads)
if(WIN32)
  include(cmake/Audio.cmake)
  target_sources(cupid_session PRIVATE
    ${PROJECT_SOURCE_DIR}/src/desktop/windows/audio/output.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/windows/audio/devices.cpp)
  target_link_libraries(cupid_session PRIVATE winmm ole32 uuid SDL3::SDL3-static)
  target_compile_definitions(cupid_session PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_sources(cupid_session PRIVATE
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/audio/output.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/audio/device.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/text.cpp)
  target_link_libraries(cupid_session PRIVATE PkgConfig::cupid_sdl2 PkgConfig::cupid_gtk3)
  add_library(cupid_linux_frontend
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/window.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/settings/settings.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/settings/dialog.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/controls/input.cpp
    ${PROJECT_SOURCE_DIR}/src/desktop/linux/controls/mapping.cpp)
  target_link_libraries(cupid_linux_frontend PUBLIC cupid_session cupid_gdb
    PkgConfig::cupid_gtk3 PkgConfig::cupid_sdl2)
  target_compile_options(cupid_linux_frontend PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_executable(cupid_desktop ${PROJECT_SOURCE_DIR}/src/desktop/linux/main.cpp)
  target_link_libraries(cupid_desktop PRIVATE cupid_linux_frontend)
  set_target_properties(cupid_desktop PROPERTIES OUTPUT_NAME cupid-n64)
  target_compile_options(cupid_desktop PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(MSVC)
  target_compile_options(cupid_session PRIVATE /W4 /WX /permissive-)
  target_precompile_headers(cupid_session PRIVATE "<desktop/session.hpp>")
else()
  target_compile_options(cupid_session PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
