# Engine core: math, geodesy, terrain generation, LOD, camera.
# No OpenGL / windowing here => fully unit-testable and reusable (tools, servers, other renderers).
find_package(Threads REQUIRED)

add_library(orbis_core STATIC
    geo/Ellipsoid.cpp
    terrain/CubeSphere.cpp
    terrain/Noise.cpp
    terrain/HeightField.cpp
    terrain/ChunkMesh.cpp
    terrain/Quadtree.cpp
    terrain/ChunkBuilder.cpp
    scene/Camera.cpp)
add_library(orbis::core ALIAS orbis_core)

# Includes are written as "core/..." so their origin is obvious at a glance.
target_include_directories(orbis_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(orbis_core PUBLIC cxx_std_20)
target_link_libraries(orbis_core PUBLIC Threads::Threads)
orbis_configure_target(orbis_core)
