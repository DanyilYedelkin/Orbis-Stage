# Shaders as string_view constants, regenerated at build time when a shader changes.
#   orbis_embed_shaders(<target> <shader files...>)
#   -> #include "EmbeddedShaders.hpp"   e.g. orbis::shaders::terrain_vert
function(orbis_embed_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(out_file "${out_dir}/EmbeddedShaders.hpp")
    list(JOIN ARGN "|" inputs)   # '|' instead of ';' so the list survives the command line

    add_custom_command(
        OUTPUT  "${out_file}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUTS=${inputs}" "-DOUTPUT=${out_file}"
                -P "${PROJECT_SOURCE_DIR}/cmake/EmbedShadersScript.cmake"
        DEPENDS ${ARGN} "${PROJECT_SOURCE_DIR}/cmake/EmbedShadersScript.cmake"
        COMMENT "Embedding shaders"
        VERBATIM)

    target_sources(${target} PRIVATE "${out_file}")
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
