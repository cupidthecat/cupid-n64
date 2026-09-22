include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

find_package(SDL3 3.4.16 EXACT CONFIG QUIET)
if(NOT TARGET SDL3::SDL3)
    set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL examples" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "Disable SDL install targets" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68)
    FetchContent_MakeAvailable(SDL3)
endif()

file(GLOB CUPID_DESKTOP_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/desktop/*.cpp")
list(FILTER CUPID_DESKTOP_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(cupid_desktop_frontend STATIC ${CUPID_DESKTOP_SOURCES})
target_include_directories(cupid_desktop_frontend PUBLIC include)
target_link_libraries(cupid_desktop_frontend PUBLIC cupid_host SDL3::SDL3)

add_executable(cupid-desktop src/desktop/main.cpp)
target_link_libraries(cupid-desktop PRIVATE cupid_desktop_frontend)

file(GLOB CUPID_DESKTOP_TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/desktop/test_*.cpp")
list(FILTER CUPID_DESKTOP_TEST_SOURCES EXCLUDE REGEX "/test_(audio|dialogs)\\.cpp$")
add_executable(cupid-desktop-tests tests/main.cpp ${CUPID_DESKTOP_TEST_SOURCES})
target_include_directories(cupid-desktop-tests PRIVATE tests src/desktop)
target_link_libraries(cupid-desktop-tests PRIVATE cupid_desktop_frontend)
add_test(NAME desktop_adapters COMMAND cupid-desktop-tests)
set_tests_properties(desktop_adapters PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1"
    LABELS "desktop")

add_executable(cupid-desktop-audio-tests tests/main.cpp tests/desktop/test_audio.cpp)
target_include_directories(cupid-desktop-audio-tests PRIVATE tests)
target_link_libraries(cupid-desktop-audio-tests PRIVATE cupid_desktop_frontend)
add_test(NAME desktop_audio COMMAND cupid-desktop-audio-tests desktop_audio_)
set_tests_properties(desktop_audio PROPERTIES
    ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;SDL_AUDIO_DUMMY_TIMESCALE=1"
    LABELS "desktop")
add_test(NAME desktop_audio_failure COMMAND cupid-desktop-audio-tests audio_open_failure_is_actionable)
set_tests_properties(desktop_audio_failure PROPERTIES
    ENVIRONMENT "SDL_AUDIO_DRIVER=cupid-invalid-audio-driver"
    LABELS "desktop")

# This executable supplies the two SDL dialog symbols to drive completion on demand.
add_executable(cupid-desktop-dialog-tests tests/main.cpp tests/desktop/test_dialogs.cpp src/desktop/dialogs.cpp)
target_include_directories(cupid-desktop-dialog-tests PRIVATE tests include)
target_compile_definitions(cupid-desktop-dialog-tests PRIVATE SDL_BUILDING_LIBRARY)
target_link_libraries(cupid-desktop-dialog-tests PRIVATE SDL3::Headers Threads::Threads)
add_test(NAME desktop_dialogs COMMAND cupid-desktop-dialog-tests desktop_file_dialog_)
set_tests_properties(desktop_dialogs PROPERTIES LABELS "desktop")

add_test(NAME desktop_runner
    COMMAND "${CMAKE_COMMAND}" -DRUNNER=$<TARGET_FILE:cupid-desktop>
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/desktop-runner
        -P ${PROJECT_SOURCE_DIR}/tests/desktop/runner.cmake)
set_tests_properties(desktop_runner PROPERTIES TIMEOUT 60 LABELS "desktop")

set(CUPID_DESKTOP_EXECUTABLES cupid-desktop cupid-desktop-tests cupid-desktop-audio-tests cupid-desktop-dialog-tests)
foreach(target cupid_desktop_frontend ${CUPID_DESKTOP_EXECUTABLES})
    cupid_configure_target(${target})
endforeach()

if(WIN32)
    get_target_property(CUPID_SDL3_ALIASED_TARGET SDL3::SDL3 ALIASED_TARGET)
    if(CUPID_SDL3_ALIASED_TARGET)
        set(CUPID_SDL3_RUNTIME_TARGET "${CUPID_SDL3_ALIASED_TARGET}")
    else()
        set(CUPID_SDL3_RUNTIME_TARGET SDL3::SDL3)
    endif()
    get_target_property(CUPID_SDL3_RUNTIME_TYPE "${CUPID_SDL3_RUNTIME_TARGET}" TYPE)
    if(CUPID_SDL3_RUNTIME_TYPE STREQUAL "SHARED_LIBRARY")
        foreach(target cupid-desktop cupid-desktop-tests cupid-desktop-audio-tests)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:${CUPID_SDL3_RUNTIME_TARGET}>" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endforeach()
    endif()
endif()

if(CUPID_SANITIZERS AND WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CUPID_SANITIZER_DLL)
    foreach(target ${CUPID_DESKTOP_EXECUTABLES})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CUPID_SANITIZER_DLL}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endforeach()
endif()
