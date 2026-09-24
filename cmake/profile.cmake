set(CUPID_PROFILE_GENERATE "" CACHE PATH "Collect Clang PGO counts in this directory")
set(CUPID_PROFILE_USE "" CACHE FILEPATH "Use this merged Clang PGO profile for cupid_core and cupid_host")
set(CUPID_PROFILE_MANIFEST "" CACHE FILEPATH "Manifest that binds CUPID_PROFILE_USE to source and toolchain")

if(CUPID_PROFILE_GENERATE AND CUPID_PROFILE_USE)
    message(FATAL_ERROR "CUPID_PROFILE_GENERATE and CUPID_PROFILE_USE are mutually exclusive")
endif()
if(CUPID_PROFILE_MANIFEST AND NOT CUPID_PROFILE_USE)
    message(FATAL_ERROR "CUPID_PROFILE_MANIFEST is only valid with CUPID_PROFILE_USE")
endif()
if(NOT CUPID_PROFILE_GENERATE AND NOT CUPID_PROFILE_USE)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR MSVC)
    message(FATAL_ERROR "Profile-guided builds currently require upstream Clang with the GNU-compatible driver")
endif()
if(CMAKE_CONFIGURATION_TYPES OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Profile-guided builds currently require a single-config Release build")
endif()
if(NOT CMAKE_CXX_STANDARD EQUAL 20 OR CMAKE_CXX_EXTENSIONS)
    message(FATAL_ERROR "Profile-guided builds require the project's C++20 configuration")
endif()
if(CMAKE_CXX_COMPILER_TARGET OR CMAKE_SYSROOT OR CMAKE_CXX_COMPILER_ARG1 OR CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
    OR CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_CXX_LINKER_LAUNCHER)
    message(FATAL_ERROR "Cross-target and compiler-wrapper profile builds are not supported")
endif()
if(CMAKE_PROJECT_INCLUDE OR CMAKE_PROJECT_INCLUDE_BEFORE OR CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
    message(FATAL_ERROR "Profile-guided builds require a build without external project hooks")
endif()
if(CUPID_SANITIZERS)
    message(FATAL_ERROR "Profile-guided builds cannot be combined with sanitizers")
endif()
if(NOT CUPID_IPO OR NOT CUPID_IPO_SUPPORTED)
    message(FATAL_ERROR "Profile-guided builds require supported interprocedural optimization")
endif()

set(profile_target_settings)
foreach(target cupid_core cupid_host)
    get_target_property(profile_ipo ${target} INTERPROCEDURAL_OPTIMIZATION_RELEASE)
    if(NOT profile_ipo)
        message(FATAL_ERROR "Profile-guided builds require Release IPO on ${target}")
    endif()
    get_target_property(profile_compile_options ${target} COMPILE_OPTIONS)
    foreach(required -fno-fast-math -frounding-math)
        list(FIND profile_compile_options "${required}" required_index)
        if(required_index EQUAL -1)
            message(FATAL_ERROR "Profile-guided builds require the existing strict floating-point flags on ${target}")
        endif()
    endforeach()
    foreach(property COMPILE_OPTIONS COMPILE_DEFINITIONS INTERPROCEDURAL_OPTIMIZATION_RELEASE)
        get_target_property(setting ${target} ${property})
        if(NOT setting)
            set(setting "")
        endif()
        # Preserve list separators and escaped characters in one command argument.
        string(HEX "${setting}" setting)
        list(APPEND profile_target_settings "--target-setting=${target}.${property}=${setting}")
    endforeach()
endforeach()

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" --print-target-triple
    OUTPUT_VARIABLE CUPID_PROFILE_TARGET_TRIPLE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT CUPID_PROFILE_TARGET_TRIPLE)
    message(FATAL_ERROR "Clang did not report a target triple for the profile build")
endif()

set(CUPID_PROFILE_COMPILED_SOURCES_FILE "${CMAKE_CURRENT_BINARY_DIR}/cupid-profile-compiled-sources.txt")
set(profile_sources ${CUPID_CORE_SOURCES} ${CUPID_HOST_SOURCES})
set(profile_relative_sources)
foreach(source IN LISTS profile_sources)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
    string(REPLACE "\\" "/" relative "${relative}")
    list(APPEND profile_relative_sources "${relative}")
endforeach()
list(REMOVE_DUPLICATES profile_relative_sources)
list(SORT profile_relative_sources)
string(REPLACE ";" "\n" profile_relative_sources_text "${profile_relative_sources}")
file(WRITE "${CUPID_PROFILE_COMPILED_SOURCES_FILE}" "${profile_relative_sources_text}\n")

find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
set(profile_identity_arguments
    --source-root "${PROJECT_SOURCE_DIR}"
    --compiled-sources "${CUPID_PROFILE_COMPILED_SOURCES_FILE}"
    --compiler "${CMAKE_CXX_COMPILER}"
    --compiler-id "${CMAKE_CXX_COMPILER_ID}"
    --compiler-version "${CMAKE_CXX_COMPILER_VERSION}"
    --target-triple "${CUPID_PROFILE_TARGET_TRIPLE}"
    --build-type "${CMAKE_BUILD_TYPE}"
    --ipo ON --sanitizers OFF
    "--cxx-flags=${CMAKE_CXX_FLAGS}"
    "--release-flags=${CMAKE_CXX_FLAGS_RELEASE}"
    "--linker-flags=${CMAKE_EXE_LINKER_FLAGS}"
    "--release-linker-flags=${CMAKE_EXE_LINKER_FLAGS_RELEASE}"
    ${profile_target_settings})
file(GLOB_RECURSE profile_headers CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/*.hpp" "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/include/*.hh" "${PROJECT_SOURCE_DIR}/include/*.inc"
    "${PROJECT_SOURCE_DIR}/include/*.inl"
    "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.hh" "${PROJECT_SOURCE_DIR}/src/*.inc"
    "${PROJECT_SOURCE_DIR}/src/*.inl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${profile_sources} ${profile_headers} "${PROJECT_SOURCE_DIR}/tools/ci/profile.py" "${CMAKE_CXX_COMPILER}")

if(CUPID_PROFILE_GENERATE)
    get_filename_component(profile_generate "${CUPID_PROFILE_GENERATE}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    file(MAKE_DIRECTORY "${profile_generate}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/ci/profile.py" snapshot
            ${profile_identity_arguments} --manifest "${profile_generate}/context.json"
        RESULT_VARIABLE profile_snapshot_result
        ERROR_VARIABLE profile_snapshot_error)
    if(NOT profile_snapshot_result EQUAL 0)
        message(FATAL_ERROR "Profile training context failed:\n${profile_snapshot_error}")
    endif()
    set(profile_generate_flag "-fprofile-generate=${profile_generate}")
    foreach(target cupid_core cupid_host)
        target_compile_options(${target} PRIVATE "${profile_generate_flag}" -fprofile-update=atomic)
    endforeach()
    # Static libraries do not link the instrumentation runtime themselves. Propagate it to every project consumer.
    target_link_options(cupid_core INTERFACE "${profile_generate_flag}")
    message(STATUS "Clang profile generation enabled for cupid_core and cupid_host")
    return()
endif()

if(NOT CUPID_PROFILE_MANIFEST)
    message(FATAL_ERROR "CUPID_PROFILE_USE requires CUPID_PROFILE_MANIFEST")
endif()
get_filename_component(profile_use "${CUPID_PROFILE_USE}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
get_filename_component(profile_manifest "${CUPID_PROFILE_MANIFEST}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
if(NOT EXISTS "${profile_use}")
    message(FATAL_ERROR "Profile payload does not exist: ${profile_use}")
endif()
if(NOT EXISTS "${profile_manifest}")
    message(FATAL_ERROR "Profile manifest does not exist: ${profile_manifest}")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/ci/profile.py" verify
        ${profile_identity_arguments}
        --profile "${profile_use}"
        --manifest "${profile_manifest}"
    RESULT_VARIABLE profile_verify_result
    OUTPUT_VARIABLE profile_verify_output
    ERROR_VARIABLE profile_verify_error)
if(NOT profile_verify_result EQUAL 0)
    message(FATAL_ERROR "Profile package verification failed:\n${profile_verify_output}${profile_verify_error}")
endif()

# Reconfigure when a package is replaced in place. The content-addressed build copy also changes the compile command,
# so an updated package cannot silently reuse objects compiled with the previous profile.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${profile_use}" "${profile_manifest}")
file(SHA256 "${profile_use}" profile_sha256)
string(STRIP "${profile_verify_output}" profile_verified_sha256)
if(NOT profile_sha256 STREQUAL profile_verified_sha256)
    message(FATAL_ERROR "The profile changed after its package was verified")
endif()
set(profile_copy_directory "${CMAKE_CURRENT_BINARY_DIR}/cupid-profile")
file(MAKE_DIRECTORY "${profile_copy_directory}")
set(profile_copy "${profile_copy_directory}/${profile_sha256}.profdata")
configure_file("${profile_use}" "${profile_copy}" COPYONLY)
file(SHA256 "${profile_copy}" profile_copy_sha256)
if(NOT profile_copy_sha256 STREQUAL profile_sha256)
    message(FATAL_ERROR "The profile changed while creating its build copy")
endif()

foreach(target cupid_core cupid_host)
    target_compile_options(${target} PRIVATE
        "-fprofile-use=${profile_copy}"
        -Werror=profile-instr-out-of-date
        -Werror=profile-instr-unprofiled)
endforeach()
message(STATUS "Clang profile use enabled for cupid_core and cupid_host: ${profile_sha256}")
