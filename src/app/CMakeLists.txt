add_executable(orbis
    main.cpp
    Application.cpp
    Window.cpp
    Terrain.cpp
    Sky.cpp
    DataFiles.cpp
    LandCoverTexture.cpp
    DetailTexture.cpp
    Clouds.cpp
    ToneMap.cpp
    Atmosphere.cpp
    ShadowCascades.cpp
    DebugUi.cpp
    ApplicationUi.cpp
    gl/ShaderProgram.cpp
    gl/RenderTarget.cpp
    gl/DebugOutput.cpp)

orbis_embed_shaders(orbis
    "${PROJECT_SOURCE_DIR}/shaders/common.glsl"
    "${PROJECT_SOURCE_DIR}/shaders/atmosphere.glsl"
    "${PROJECT_SOURCE_DIR}/shaders/transmittance.comp"
    "${PROJECT_SOURCE_DIR}/shaders/multiscattering.comp"
    "${PROJECT_SOURCE_DIR}/shaders/irradiance.comp"
    "${PROJECT_SOURCE_DIR}/shaders/clouds.glsl"
    "${PROJECT_SOURCE_DIR}/shaders/cloud_noise.comp"
    "${PROJECT_SOURCE_DIR}/shaders/clouds.frag"
    "${PROJECT_SOURCE_DIR}/shaders/tonemap.frag"
    "${PROJECT_SOURCE_DIR}/shaders/terrain.vert"
    "${PROJECT_SOURCE_DIR}/shaders/terrain.frag"
    "${PROJECT_SOURCE_DIR}/shaders/sky.vert"
    "${PROJECT_SOURCE_DIR}/shaders/sky.frag"
    "${PROJECT_SOURCE_DIR}/shaders/shadow.frag")

target_link_libraries(orbis PRIVATE orbis::core glad glfw imgui)
# GLFW must not pull in its own GL header: glad provides the declarations.
target_compile_definitions(orbis PRIVATE
    GLFW_INCLUDE_NONE
    ORBIS_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

# Ship the data next to the executable, so the built program also works when copied elsewhere.
add_custom_command(TARGET orbis POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:orbis>/data"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/data/earth_etopo10.oeg"
            "${PROJECT_SOURCE_DIR}/data/earth_landcover.olc" "$<TARGET_FILE_DIR:orbis>/data/"
    COMMENT "Copying Earth elevation and land cover data"
    VERBATIM)
orbis_configure_target(orbis)

if(MSVC)
    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT orbis)
endif()
