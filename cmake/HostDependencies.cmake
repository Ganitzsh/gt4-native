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
# The Vulkan headers and volk are pinned to the commits paraLLEl-GS's Granite uses, so the host and the
# GS back end share one copy of each (one set of volk globals in the executable).
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG 11d6898377797e07dbd543aaaa367e4465074597) # header version 358 (Granite's submodule)

# paraLLEl-GS (LGPL-3.0-or-later, Arntzen Software AS): the GS emulated with Vulkan compute shaders, with
# its Vulkan layer Granite (MIT, Hans-Kristian Arntzen) as a submodule. Only Granite itself is checked
# out (not its own submodules); volk goes into Granite's third_party/volk below. Its CMake is added
# further down in standalone (library) mode.
FetchContent_Declare(parallel_gs
    GIT_REPOSITORY https://github.com/Arntzen-Software/parallel-gs.git
    GIT_TAG cc6184af7e0c03da603045ca371ffa5dae9b0655 # 2026-10-06, Granite 16e7395f
    GIT_SUBMODULES Granite
    GIT_SUBMODULES_RECURSE OFF
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

FetchContent_MakeAvailable(SDL3 VulkanHeaders parallel_gs glslang)

# volk (MIT): the Vulkan meta-loader, at Granite's pinned commit, checked out where Granite expects it.
# Granite compiles it (target granite-volk); gt4_host links that one copy.
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 47cddf7ed97b94118a08aacb548a411188e016cc # Granite's submodule (VOLK_HEADER_VERSION 352)
    SOURCE_DIR ${parallel_gs_SOURCE_DIR}/Granite/third_party/volk
    SOURCE_SUBDIR gt4-sources-only)
FetchContent_MakeAvailable(volk)

# paraLLEl-GS as a library: no SDL platform, no runtime shader compiler (its SPIR-V is precompiled into
# gs/shaders/slangmosh.hpp), no Fossilize, no RenderDoc hook. Vulkan::Headers already exists, so Granite
# uses ours. Only the targets gt4_host links are built (EXCLUDE_FROM_ALL).
set(PARALLEL_GS_STANDALONE ON CACHE BOOL "" FORCE)
set(GRANITE_RENDERDOC_CAPTURE OFF CACHE BOOL "" FORCE)
set(GRANITE_VULKAN_DXGI_INTEROP OFF CACHE BOOL "" FORCE)
set(GRANITE_TOOLS OFF CACHE BOOL "" FORCE)
set(GRANITE_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
# Granite declares an old CMake minimum; give its targets the same MSVC runtime selection as ours (/MD).
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
if(NOT CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_subdirectory(${parallel_gs_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_deps/parallel_gs-lib EXCLUDE_FROM_ALL)
