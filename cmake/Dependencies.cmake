# Pinned releases, verified by SHA-256.
include(FetchContent)

if(ORBIS_BUILD_APP)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    # XWayland covers Wayland desktops.
    set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(glfw
        URL      https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
        URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM)
    FetchContent_MakeAvailable(glfw)

    # No CMake build upstream.
    FetchContent_Declare(imgui
        URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.tar.gz
        URL_HASH SHA256=2aa2d169c569368439e5d5667e0796d09ca5cc6432965ce082e516937d7db254
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui
        PUBLIC  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(imgui PUBLIC glfw)
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
