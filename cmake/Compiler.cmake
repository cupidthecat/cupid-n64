include(FetchContent)

FetchContent_Declare(cpu_compiler
  URL https://codeload.github.com/zherczeg/sljit/tar.gz/de0259c7aaf36aa40cba8014f3fad3edde9307f9
  URL_HASH SHA256=256b50b4e96c806fd13fc9b630f6c1ba41b777e7cedfcf406651d062d8260611
  SOURCE_SUBDIR .cupid-package
)
FetchContent_MakeAvailable(cpu_compiler)
add_library(cupid_compiler_dependency STATIC "${cpu_compiler_SOURCE_DIR}/sljit_src/sljitLir.c")
target_include_directories(cupid_compiler_dependency SYSTEM PUBLIC
  "${cpu_compiler_SOURCE_DIR}/sljit_src"
)
target_compile_definitions(cupid_compiler_dependency PUBLIC
  SLJIT_VERBOSE=0
  SLJIT_DEBUG=$<IF:$<CONFIG:Debug>,1,0>
)
target_link_libraries(cupid_core PRIVATE cupid_compiler_dependency)
