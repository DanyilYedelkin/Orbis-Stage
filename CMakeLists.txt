cmake_minimum_required(VERSION 3.25)

project(Orbis
    VERSION 0.1.0
    DESCRIPTION "Planet-scale terrain renderer"
    LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_property(ORBIS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT ORBIS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ORBIS_BUILD_APP          "Build the interactive renderer (needs a GPU / OpenGL 4.5)" ON)
option(ORBIS_BUILD_TESTS        "Build unit tests"                                          ON)
option(ORBIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"                          OFF)
option(ORBIS_SANITIZERS         "Enable AddressSanitizer + UBSan (GCC/Clang only)"           OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(Dependencies)
include(EmbedShaders)

add_subdirectory(src/core)

if(ORBIS_BUILD_APP)
    add_subdirectory(third_party/glad)
    add_subdirectory(src/app)
endif()

if(ORBIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
