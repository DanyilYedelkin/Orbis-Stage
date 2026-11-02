# Applies the project-wide warning / sanitizer policy to one of OUR targets.
# Third-party targets are deliberately left alone: we cannot fix their warnings.
function(orbis_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(ORBIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow                 # a local hides another variable
            -Wconversion             # implicit narrowing, e.g. double -> float
            -Wsign-conversion        # implicit signed <-> unsigned
            -Wdouble-promotion       # accidental float -> double math
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
    endif()
endfunction()
