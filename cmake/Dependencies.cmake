# All third-party code is pinned to an exact release AND verified by SHA-256.
# Builds are reproducible and a tampered archive fails the build instead of silently compiling.
include(FetchContent)

if(ORBIS_BUILD_APP)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    # X11 is enough on Linux (Wayland desktops run it through XWayland) and needs fewer packages.
    set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(glfw
        URL      https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    FetchContent_MakeAvailable(glfw)
endif()

if(ORBIS_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
        URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
