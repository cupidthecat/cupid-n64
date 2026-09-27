function(cupid_configure_target target)
    if(TARGET cupid_sljit)
        get_target_property(target_type ${target} TYPE)
        if(target_type STREQUAL "EXECUTABLE")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${PROJECT_SOURCE_DIR}/third_party/sljit/LICENSE"
                    "$<TARGET_FILE_DIR:${target}>/licenses/sljit.txt"
                VERBATIM)
        endif()
    endif()
    if(CUPID_IPO_SUPPORTED)
        set_target_properties(${target} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
            INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /fp:strict /utf-8)
        if(CUPID_STRICT)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -fno-fast-math -frounding-math)
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
endfunction()
