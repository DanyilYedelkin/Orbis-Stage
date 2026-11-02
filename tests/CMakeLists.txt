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
    test_chunk_builder.cpp)

target_link_libraries(orbis_tests PRIVATE orbis::core Catch2::Catch2WithMain)
orbis_configure_target(orbis_tests)

include(Catch)
catch_discover_tests(orbis_tests)
