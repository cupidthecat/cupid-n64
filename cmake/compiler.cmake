function(cupid_configure_target target)
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
