add_executable(orbis_tests
    test_vec3.cpp
    test_mat4.cpp
    test_ellipsoid.cpp
    test_cube_sphere.cpp
    test_noise.cpp
    test_chunk_mesh.cpp
    test_camera.cpp
    test_lod.cpp
    test_culling.cpp
    test_chunk_builder.cpp
    test_earth.cpp
    test_atmosphere.cpp)

target_link_libraries(orbis_tests PRIVATE orbis::core Catch2::Catch2WithMain)
target_compile_definitions(orbis_tests PRIVATE ORBIS_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
orbis_configure_target(orbis_tests)

include(Catch)
catch_discover_tests(orbis_tests)
