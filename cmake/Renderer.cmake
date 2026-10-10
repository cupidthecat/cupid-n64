include(FetchContent)

FetchContent_Declare(renderer_rdp
  URL https://codeload.github.com/Themaister/parallel-rdp/tar.gz/1cecd042b2619bc505c12bfdc713808386f2b54d
  URL_HASH SHA256=5484ec46ac0a877272d645e6ea80878424a527d0d0ecfd13af1e27a537c53529
  SOURCE_SUBDIR .cupid-package
)
FetchContent_Declare(renderer_backend
  URL https://codeload.github.com/Themaister/Granite/tar.gz/cf71dee71fb00110749a9a3fcbd87b0297e49b6a
  URL_HASH SHA256=d18286f67190b71b1cd80c1a37da57f5801ae76a955e5bb5294c898da45f232e
  SOURCE_SUBDIR .cupid-package
)
FetchContent_Declare(renderer_headers
  URL https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/31aa7f634b052d87ede4664053e85f3f4d1d50d3
  URL_HASH SHA256=a4b8c15f4bbbc9feb007bfb7131f0fb390af47508368fc765b99472cc4c74159
  SOURCE_SUBDIR .cupid-package
)
FetchContent_Declare(renderer_loader
  URL https://codeload.github.com/zeux/volk/tar.gz/41c8f70e805142d529653d8bc87a5b2f259c619d
  URL_HASH SHA256=c91629257f5cec19cdf55b7c652bf120d6fa08d8fd93f1d7acf7ae09fddf4c41
  SOURCE_SUBDIR .cupid-package
)
FetchContent_MakeAvailable(renderer_rdp renderer_backend renderer_headers renderer_loader)
include(cmake/RendererState.cmake)

set(renderer_sources)
foreach(source command_ring rdp_device rdp_dump_write rdp_renderer video_interface)
  list(APPEND renderer_sources "${renderer_state_sources}/${source}.cpp")
endforeach()
foreach(source
  buffer buffer_pool command_buffer command_pool context cookie descriptor_set device
  event_manager fence fence_manager image indirect_layout memory_allocator pipeline_event
  query_pool render_pass sampler semaphore semaphore_manager shader texture/texture_format
)
  list(APPEND renderer_sources "${renderer_backend_SOURCE_DIR}/vulkan/${source}.cpp")
endforeach()
foreach(source
  arena_allocator logging thread_id aligned_alloc timer timeline_trace_file thread_name environment
)
  list(APPEND renderer_sources "${renderer_backend_SOURCE_DIR}/util/${source}.cpp")
endforeach()
list(APPEND renderer_sources "${renderer_loader_SOURCE_DIR}/volk.c")
add_library(cupid_renderer_dependency STATIC ${renderer_sources})
target_compile_features(cupid_renderer_dependency PUBLIC cxx_std_17)
target_include_directories(cupid_renderer_dependency SYSTEM PUBLIC
  "${renderer_state_sources}"
  "${renderer_backend_SOURCE_DIR}/vulkan"
  "${renderer_backend_SOURCE_DIR}/util"
  "${renderer_loader_SOURCE_DIR}"
  "${renderer_headers_SOURCE_DIR}/include"
)
target_include_directories(cupid_renderer_dependency PRIVATE
  "${PROJECT_SOURCE_DIR}/third_party/renderer"
)
find_package(Threads REQUIRED)
target_link_libraries(cupid_renderer_dependency PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  target_compile_definitions(cupid_renderer_dependency PUBLIC VK_USE_PLATFORM_WIN32_KHR)
  target_link_libraries(cupid_renderer_dependency PUBLIC winmm)
endif()

add_library(cupid_renderer
  src/renderer/vulkan/renderer.cpp
  src/renderer/state/renderer_state.cpp
  src/renderer/state/registers.cpp
  src/renderer/state/machine_state.cpp
  src/renderer/video/readback.cpp
)
target_link_libraries(cupid_renderer PUBLIC cupid_core PRIVATE cupid_renderer_dependency)
target_include_directories(cupid_renderer PUBLIC src)
target_compile_features(cupid_renderer PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(cupid_renderer PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(cupid_renderer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
