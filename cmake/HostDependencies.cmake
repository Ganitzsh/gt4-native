# Pinned third-party dependencies of gt4_host. Everything is fetched at configure time, so CI and a
# fresh checkout need only CMake, a C++20 compiler and (on Linux) SDL's system headers.
include(FetchContent)

# GT4_GS_REFERENCE: paraLLEl-GS, a GS emulator, built as a reference for the native renderer's tests only
# (tests/gs_backend.cpp, tests/gs_native.cpp). gt4_host never contains it (docs/NATIVE_ONLY.md).
option(GT4_GS_REFERENCE "Build paraLLEl-GS as a test reference for the native renderer (not linked into gt4_host)" OFF)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 7f3ae3d57459e59943a4ecfefc8f6277ec6bf540) # release-3.2.28
# The Vulkan headers and volk are pinned to the commits paraLLEl-GS's Granite uses, so a GT4_GS_REFERENCE
# build shares one copy of each with it.
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG 11d6898377797e07dbd543aaaa367e4465074597) # header version 358 (Granite's submodule)

# paraLLEl-GS (LGPL-3.0-or-later, Arntzen Software AS), GT4_GS_REFERENCE only: the GS emulated with Vulkan
# compute shaders, with its Vulkan layer Granite (MIT, Hans-Kristian Arntzen) as a submodule. Only Granite
# itself is checked out (not its own submodules); volk goes into Granite's third_party/volk below. Its CMake
# is added further down in standalone (library) mode.
if(GT4_GS_REFERENCE)
FetchContent_Declare(parallel_gs
    GIT_REPOSITORY https://github.com/Arntzen-Software/parallel-gs.git
    GIT_TAG cc6184af7e0c03da603045ca371ffa5dae9b0655 # 2026-10-06, Granite 16e7395f
    GIT_SUBMODULES Granite
    GIT_SUBMODULES_RECURSE OFF
    SOURCE_SUBDIR gt4-sources-only)
endif()

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

FetchContent_MakeAvailable(SDL3 VulkanHeaders glslang)

# volk (MIT): the Vulkan meta-loader, at Granite's pinned commit. With GT4_GS_REFERENCE it is checked out where
# Granite expects it and Granite compiles it (granite-volk); otherwise gt4-volk below. GT4_VOLK and
# GT4_VOLK_HEADERS name the targets either way (one copy of volk's globals in each executable).
if(GT4_GS_REFERENCE)
  FetchContent_MakeAvailable(parallel_gs)
  FetchContent_Declare(volk
      GIT_REPOSITORY https://github.com/zeux/volk.git
      GIT_TAG 47cddf7ed97b94118a08aacb548a411188e016cc # Granite's submodule (VOLK_HEADER_VERSION 352)
      SOURCE_DIR ${parallel_gs_SOURCE_DIR}/Granite/third_party/volk
      SOURCE_SUBDIR gt4-sources-only)
else()
  FetchContent_Declare(volk
      GIT_REPOSITORY https://github.com/zeux/volk.git
      GIT_TAG 47cddf7ed97b94118a08aacb548a411188e016cc # Granite's submodule (VOLK_HEADER_VERSION 352)
      SOURCE_SUBDIR gt4-sources-only)
endif()
FetchContent_MakeAvailable(volk)
if(NOT GT4_GS_REFERENCE)
  enable_language(C)   # volk.c
  add_library(gt4-volk-headers INTERFACE)
  target_include_directories(gt4-volk-headers INTERFACE ${volk_SOURCE_DIR})
  target_link_libraries(gt4-volk-headers INTERFACE Vulkan::Headers)
  add_library(gt4-volk STATIC ${volk_SOURCE_DIR}/volk.c ${volk_SOURCE_DIR}/volk.h)
  if(WIN32)
    target_compile_definitions(gt4-volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
  else()
    target_link_libraries(gt4-volk PRIVATE ${CMAKE_DL_LIBS})
  endif()
  target_link_libraries(gt4-volk PRIVATE gt4-volk-headers)
  set(GT4_VOLK gt4-volk)
  set(GT4_VOLK_HEADERS gt4-volk-headers)
  return()
endif()
set(GT4_VOLK granite-volk)
set(GT4_VOLK_HEADERS granite-volk-headers)

# paraLLEl-GS as a library: no SDL platform, no runtime shader compiler (its SPIR-V is precompiled into
# gs/shaders/slangmosh.hpp), no Fossilize, no RenderDoc hook. Vulkan::Headers already exists, so Granite
# uses ours. Only the targets the reference tests link are built (EXCLUDE_FROM_ALL).
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
