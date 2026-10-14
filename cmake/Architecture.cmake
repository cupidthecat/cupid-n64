include_guard(GLOBAL)

option(CUPID_MINIMUM_CPU "Enable the x86-64-v2 build baseline" ON)

function(cupid_target_architecture target)
  if(NOT CUPID_MINIMUM_CPU OR
     CMAKE_CXX_COMPILER_TARGET MATCHES "^(aarch64|arm)" OR
     CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(ARM|arm|aarch64)" OR
     CMAKE_GENERATOR_PLATFORM MATCHES "^(ARM|arm)" OR
     NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MSVC)
    target_compile_options(${target} PUBLIC /clang:-march=x86-64-v2)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PUBLIC
      "$<$<NOT:$<CONFIG:Debug>>:-march=x86-64-v2>")
  endif()
endfunction()
