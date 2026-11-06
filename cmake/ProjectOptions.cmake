# Warnings and sanitizers for first-party targets.
function(orbis_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(ORBIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual)
        if(ORBIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(ORBIS_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
        if(ORBIS_TSAN)
            target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=thread)
        endif()
    endif()
endfunction()
