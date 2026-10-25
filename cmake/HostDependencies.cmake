# Pinned third-party dependencies of gt4_host. Everything is fetched at configure time, so CI and a
# fresh checkout need only CMake, a C++20 compiler and (on Linux) SDL's system headers.
include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 7f3ae3d57459e59943a4ecfefc8f6277ec6bf540) # release-3.2.28
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG 4c8aa84a61c12cab9780fa44298617666b491d71) # header version 321

# volk (MIT): the Vulkan meta-loader. Only its sources are used (SOURCE_SUBDIR points at a directory
# without a CMakeLists.txt, so its own build is not added); volk.c is compiled into gt4_host.
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 4f3bcee79618a9abe79f4c717c50379197c77512 # vulkan-sdk-1.4.335.0
    SOURCE_SUBDIR gt4-sources-only)

# glslang (BSD-3-Clause and others, see its LICENSE.txt): the reference GLSL compiler, built as a host
# tool to compile the shaders to SPIR-V during the build. Nothing from it is linked into gt4_host.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG b5782e52ee2f7b3e40bb9c80d15b47016e008bc9) # vulkan-sdk-1.4.335.0

FetchContent_MakeAvailable(SDL3 VulkanHeaders volk glslang)
