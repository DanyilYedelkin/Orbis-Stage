add_executable(orbis
    main.cpp
    Application.cpp
    Window.cpp
    Terrain.cpp
    Sky.cpp
    gl/ShaderProgram.cpp
    gl/RenderTarget.cpp
    gl/DebugOutput.cpp)

orbis_embed_shaders(orbis
    "${PROJECT_SOURCE_DIR}/shaders/common.glsl"
    "${PROJECT_SOURCE_DIR}/shaders/terrain.vert"
    "${PROJECT_SOURCE_DIR}/shaders/terrain.frag"
    "${PROJECT_SOURCE_DIR}/shaders/sky.vert"
    "${PROJECT_SOURCE_DIR}/shaders/sky.frag")

target_link_libraries(orbis PRIVATE orbis::core glad glfw)
# GLFW must not pull in its own GL header: glad provides the declarations.
target_compile_definitions(orbis PRIVATE GLFW_INCLUDE_NONE)
orbis_configure_target(orbis)

if(MSVC)
    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT orbis)
endif()
