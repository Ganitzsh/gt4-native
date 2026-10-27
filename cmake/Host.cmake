# gt4_host targets: shaders compiled to SPIR-V by glslang at build time and embedded as C++ arrays,
# AMD FidelityFX FSR 2 (vendored, MIT) and the host executable.

set(GT4_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/platform/render/shaders)
set(GT4_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/gt4_shaders)
set(GT4_FSR2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fidelityfx-fsr2)
file(MAKE_DIRECTORY ${GT4_SHADER_OUT})
file(GLOB GT4_SHADER_INCLUDES CONFIGURE_DEPENDS
    ${GT4_SHADER_SOURCE}/*.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl
    ${GT4_FSR2_DIR}/shaders/*.h)
set(GT4_SHADER_NAMES "")
set(GT4_SHADER_HEADERS "")

# gt4_shader(<name> <source> <stage> [ENV vulkan1.x] [DEFINES A=1 B ...])
function(gt4_shader name source stage)
  cmake_parse_arguments(PARSE_ARGV 3 SHADER "" "ENV" "DEFINES")
  if(NOT SHADER_ENV)
    set(SHADER_ENV vulkan1.3)
  endif()
  set(defines "")
  foreach(define IN LISTS SHADER_DEFINES)
    list(APPEND defines "-D${define}")
  endforeach()
  set(output ${GT4_SHADER_OUT}/spv_${name}.h)
  add_custom_command(OUTPUT ${output}
      COMMAND glslang-standalone -V --target-env ${SHADER_ENV} -S ${stage} ${defines}
              -I${GT4_SHADER_SOURCE} -I${CMAKE_CURRENT_SOURCE_DIR}/third_party
              --vn spv_${name} -o ${output} ${source}
      DEPENDS ${source} ${GT4_SHADER_INCLUDES}
      COMMENT "Compiling shader ${name}"
      VERBATIM)
  set(GT4_SHADER_NAMES ${GT4_SHADER_NAMES} ${name} PARENT_SCOPE)
  set(GT4_SHADER_HEADERS ${GT4_SHADER_HEADERS} ${output} PARENT_SCOPE)
endfunction()

set(S ${GT4_SHADER_SOURCE})
# Test scene (stands in for the R1 game renderer).
gt4_shader(scene_vert ${S}/scene.vert vert)
gt4_shader(scene_frag ${S}/scene.frag frag)
gt4_shader(sky_frag ${S}/sky.frag frag)
gt4_shader(shadow_vert ${S}/shadow.vert vert)
gt4_shader(envmap_comp ${S}/envmap.comp comp)
gt4_shader(envfilter_comp ${S}/envfilter.comp comp)
gt4_shader(fullscreen_vert ${S}/fullscreen.vert vert)
# The PS2's displayed frame (GS back end) as the scene.
gt4_shader(gs_frame_frag ${S}/gs_frame.frag frag)
# Post-processing and output.
gt4_shader(msaa_resolve_comp ${S}/msaa_resolve.comp comp)
gt4_shader(exposure_histogram_comp ${S}/exposure_histogram.comp comp)
gt4_shader(exposure_adapt_comp ${S}/exposure_adapt.comp comp)
gt4_shader(taa_comp ${S}/taa.comp comp)
gt4_shader(bloom_down_comp ${S}/bloom_down.comp comp)
gt4_shader(bloom_up_comp ${S}/bloom_up.comp comp)
gt4_shader(dof_prepare_comp ${S}/dof_prepare.comp comp)
gt4_shader(dof_gather_comp ${S}/dof_gather.comp comp)
gt4_shader(dof_composite_comp ${S}/dof_composite.comp comp)
gt4_shader(mb_tilemax_comp ${S}/mb_tilemax.comp comp)
gt4_shader(mb_neighbormax_comp ${S}/mb_neighbormax.comp comp)
gt4_shader(mb_gather_comp ${S}/mb_gather.comp comp)
gt4_shader(tonemap_comp ${S}/tonemap.comp comp)
gt4_shader(fxaa_comp ${S}/fxaa.comp comp)
gt4_shader(smaa_edge_vert ${S}/smaa.vert vert DEFINES SMAA_PASS=0)
gt4_shader(smaa_edge_frag ${S}/smaa.frag frag DEFINES SMAA_PASS=0)
gt4_shader(smaa_weight_vert ${S}/smaa.vert vert DEFINES SMAA_PASS=1)
gt4_shader(smaa_weight_frag ${S}/smaa.frag frag DEFINES SMAA_PASS=1)
gt4_shader(smaa_blend_vert ${S}/smaa.vert vert DEFINES SMAA_PASS=2)
gt4_shader(smaa_blend_frag ${S}/smaa.frag frag DEFINES SMAA_PASS=2)
gt4_shader(smaa_resolve_comp ${S}/smaa_resolve.comp comp)
gt4_shader(resample_comp ${S}/resample.comp comp)
gt4_shader(easu_comp ${S}/easu.comp comp)
gt4_shader(rcas_comp ${S}/rcas.comp comp)
gt4_shader(final_frag ${S}/final.frag frag)
gt4_shader(overlay_vert ${S}/overlay.vert vert)
gt4_shader(overlay_frag ${S}/overlay.frag frag)

# FSR 2 passes, compiled with the options this host always uses: HDR input, render-resolution motion
# vectors, unjittered motion vectors, inverted (reverse-Z) depth, 32-bit floats. The accumulate pass has
# a sharpening and a plain variant. These replace the precompiled permutation tables of the SDK.
set(GT4_FSR2_DEFINES FFX_GLSL=1 FFX_GPU=1 FFX_HALF=0 FFX_FSR2_OPTION_HDR_COLOR_INPUT=1
    FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1 FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS=0
    FFX_FSR2_OPTION_INVERTED_DEPTH=1 FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE=0)
# FSR 2.2.1's GLSL declares two storage images with formats other than those of the textures its runtime
# creates (rw_dilatedDepth r16f for an R32_SFLOAT texture, rw_prepared_input_color rgba16 for an
# R16G16B16A16_SFLOAT one), which Vulkan validation reports as undefined behaviour. The passes are compiled
# from a copy of the shader directory with those two qualifiers corrected; third_party/ stays as published.
set(F ${CMAKE_CURRENT_BINARY_DIR}/fsr2_shaders)
file(GLOB GT4_FSR2_SHADER_FILES ${GT4_FSR2_DIR}/shaders/*)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GT4_FSR2_SHADER_FILES})
foreach(path IN LISTS GT4_FSR2_SHADER_FILES)
  get_filename_component(file ${path} NAME)
  file(READ ${path} content)
  if(file STREQUAL "ffx_fsr2_callbacks_glsl.h")
    foreach(fix "FSR2_BIND_UAV_DILATED_DEPTH, r16f)|FSR2_BIND_UAV_DILATED_DEPTH, r32f)"
                "FSR2_BIND_UAV_PREPARED_INPUT_COLOR, rgba16)|FSR2_BIND_UAV_PREPARED_INPUT_COLOR, rgba16f)")
      string(REPLACE "|" ";" pair "${fix}")
      list(GET pair 0 old)
      list(GET pair 1 new)
      string(FIND "${content}" "${old}" found)
      if(found EQUAL -1)
        message(FATAL_ERROR "FSR 2 shader fix-up no longer applies: ${old}")
      endif()
      string(REPLACE "${old}" "${new}" content "${content}")
    endforeach()
  endif()
  file(CONFIGURE OUTPUT ${F}/${file} CONTENT "${content}" @ONLY)
endforeach()
foreach(pass tcr_autogen autogen_reactive compute_luminance_pyramid depth_clip lock reconstruct_previous_depth rcas)
  gt4_shader(fsr2_${pass} ${F}/ffx_fsr2_${pass}_pass.glsl comp ENV vulkan1.1
      DEFINES ${GT4_FSR2_DEFINES} FFX_FSR2_OPTION_APPLY_SHARPENING=0)
endforeach()
gt4_shader(fsr2_accumulate ${F}/ffx_fsr2_accumulate_pass.glsl comp ENV vulkan1.1
    DEFINES ${GT4_FSR2_DEFINES} FFX_FSR2_OPTION_APPLY_SHARPENING=0)
gt4_shader(fsr2_accumulate_sharpen ${F}/ffx_fsr2_accumulate_pass.glsl comp ENV vulkan1.1
    DEFINES ${GT4_FSR2_DEFINES} FFX_FSR2_OPTION_APPLY_SHARPENING=1)

# One translation unit holds every SPIR-V array; others call gt4::shaders::<name>().
set(header "#pragma once\n// Generated by cmake/Host.cmake: SPIR-V of every host shader.\n#include \"vk.h\"\nnamespace gt4::shaders {\n")
set(source "// Generated by cmake/Host.cmake.\n#include <cstdint>\n#include \"gt4_shaders.h\"\n")
foreach(name IN LISTS GT4_SHADER_NAMES)
  string(APPEND header "vk::ShaderCode ${name}();\n")
  string(APPEND source "#include \"spv_${name}.h\"\n")
endforeach()
string(APPEND header "}  // namespace gt4::shaders\n")
string(APPEND source "namespace gt4::shaders {\n")
foreach(name IN LISTS GT4_SHADER_NAMES)
  string(APPEND source "vk::ShaderCode ${name}() { return {spv_${name}, sizeof(spv_${name})}; }\n")
endforeach()
string(APPEND source "}  // namespace gt4::shaders\n")
file(CONFIGURE OUTPUT ${GT4_SHADER_OUT}/gt4_shaders.h CONTENT "${header}")
file(CONFIGURE OUTPUT ${GT4_SHADER_OUT}/gt4_shaders.cpp CONTENT "${source}")

# AMD FidelityFX Super Resolution 2.2.1 runtime and its Vulkan backend (third_party/fidelityfx-fsr2, MIT).
# The precompiled shader tables of the SDK are replaced by platform/render/fsr2_shaders.cpp (our SPIR-V,
# reflected at run time). Vendored code is built without our warning flags; it is not modified.
add_library(gt4_fsr2 STATIC
    ${GT4_FSR2_DIR}/ffx_fsr2.cpp
    ${GT4_FSR2_DIR}/ffx_assert.cpp
    ${GT4_FSR2_DIR}/vk/ffx_fsr2_vk.cpp
    platform/render/fsr2_shaders.cpp)
target_include_directories(gt4_fsr2 PRIVATE ${GT4_FSR2_DIR} ${GT4_SHADER_OUT} platform/render)
# FFX_GCC only makes FFX_API empty (no dllexport): FSR 2 is linked statically.
target_compile_definitions(gt4_fsr2 PRIVATE VK_NO_PROTOTYPES FFX_GCC _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    _CRT_SECURE_NO_WARNINGS)
target_link_libraries(gt4_fsr2 PRIVATE Vulkan::Headers)
target_link_libraries(gt4_fsr2 PRIVATE granite-volk-headers)
if(MSVC)
  target_compile_options(gt4_fsr2 PRIVATE /W0 /FI${CMAKE_CURRENT_SOURCE_DIR}/platform/render/ffx_compat.h)
else()
  target_compile_options(gt4_fsr2 PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/platform/render/ffx_compat.h)
endif()

# PS2 graphics (docs/GS_BACKEND.md). gt4_ps2gfx is plain C++ (no GPU): the game link and capture
# replay, the graphics stream, and VIF1 + VU1. gt4_gs adds the GS back end, paraLLEl-GS on its own
# Vulkan device, and the host that runs them on a worker thread.
option(GT4_VU1_INTERPRETER
  "Development scaffold, not for releases: run the game's VU1 microprograms in a CPU interpreter" ON)
find_package(Threads REQUIRED)
add_library(gt4_ps2gfx STATIC
    platform/gs/vu1.cpp
    platform/gs/vu1_ref.cpp
    platform/gs/vu1_inventory.cpp
    platform/gs/vu1_native.cpp
    platform/gs/vif1.cpp
    platform/gs/stream.cpp
    platform/gs/link_host.cpp)
target_include_directories(gt4_ps2gfx PUBLIC platform/gs)
target_link_libraries(gt4_ps2gfx PUBLIC gt4_guest_abi Threads::Threads)
target_compile_definitions(gt4_ps2gfx PUBLIC GT4_VU1_INTERPRETER=$<BOOL:${GT4_VU1_INTERPRETER}>
    $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)
add_library(gt4_gs STATIC platform/gs/gs_backend.cpp platform/gs/gs_host.cpp)
target_include_directories(gt4_gs PUBLIC platform/gs)
target_link_libraries(gt4_gs PUBLIC gt4_ps2gfx PRIVATE parallel-gs)
if(MSVC)
  target_compile_options(gt4_ps2gfx PRIVATE /W4)
  target_compile_options(gt4_gs PRIVATE /W3 /wd4267 /wd4244)
else()
  target_compile_options(gt4_ps2gfx PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gt4_host
    platform/sdl/main.cpp
    platform/sdl/renderer.cpp
    platform/sdl/session.cpp
    platform/sdl/settings.cpp
    platform/sdl/display.cpp
    platform/render/vk.cpp
    platform/render/pipeline.cpp
    platform/render/test_scene.cpp
    platform/render/gs_scene.cpp
    platform/render/overlay.cpp
    platform/render/fsr2.cpp
    platform/render/smaa_textures.cpp
    ${GT4_SHADER_OUT}/gt4_shaders.cpp
    ${GT4_SHADER_HEADERS})
target_include_directories(gt4_host PRIVATE platform/sdl platform/render ${GT4_SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${GT4_FSR2_DIR})
target_compile_definitions(gt4_host PRIVATE VK_NO_PROTOTYPES FFX_GCC)
target_link_libraries(gt4_host PRIVATE SDL3::SDL3 Vulkan::Headers gt4_guest_abi gt4_fsr2 gt4_gs granite-volk granite-volk-headers)
if(MSVC)
  target_compile_options(gt4_host PRIVATE /W4)
  # Generated SPIR-V tables and the volk loader are third-party style code: no warnings there.
  set_source_files_properties(${GT4_SHADER_OUT}/gt4_shaders.cpp
      platform/render/smaa_textures.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
  target_compile_options(gt4_host PRIVATE -Wall -Wextra -Wpedantic)
  set_source_files_properties(${GT4_SHADER_OUT}/gt4_shaders.cpp
      platform/render/smaa_textures.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(MINGW)
  target_link_options(gt4_host PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(gt4_host PRIVATE ${CMAKE_DL_LIBS})
endif()

# Settings and display math checks (no GPU or window needed).
if(BUILD_TESTING)
  add_executable(gt4_host_settings_test tests/host_settings.cpp platform/sdl/settings.cpp)
  target_include_directories(gt4_host_settings_test PRIVATE platform/sdl platform/render)
  if(MSVC)
    target_compile_options(gt4_host_settings_test PRIVATE /W4)
  else()
    target_compile_options(gt4_host_settings_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME host_settings COMMAND gt4_host_settings_test)

  # PS2 graphics on the CPU: VU1, VIF1, stream records, replay, pad (no GPU needed).
  add_executable(gt4_ps2gfx_test tests/ps2gfx.cpp)
  target_link_libraries(gt4_ps2gfx_test PRIVATE gt4_ps2gfx)
  add_test(NAME ps2gfx COMMAND gt4_ps2gfx_test)
  # The GS back end checked by pixel, and a capture replayed through gt4_host (headless, "original"
  # preset, compared by pixel): need a Vulkan GPU that meets paraLLEl-GS's requirements; skipped (exit
  # code 77) where there is none, as on CI. Outputs go to the build directory.
  add_executable(gt4_gs_backend_test tests/gs_backend.cpp)
  target_link_libraries(gt4_gs_backend_test PRIVATE gt4_gs)
  set(GT4_GS_TEST_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gs_test_output)
  add_test(NAME gs_backend COMMAND gt4_gs_backend_test ${GT4_GS_TEST_OUTPUT})
  set_tests_properties(gs_backend PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP gs_replay_capture)
  add_test(NAME host_replay COMMAND ${CMAKE_COMMAND} -DHOST=$<TARGET_FILE:gt4_host>
      -DCHECKER=$<TARGET_FILE:gt4_gs_backend_test> -DOUTPUT=${GT4_GS_TEST_OUTPUT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/host_replay.cmake)
  set_tests_properties(host_replay PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED gs_replay_capture)
  foreach(test_target IN ITEMS gt4_ps2gfx_test gt4_gs_backend_test)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG /W4)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG -Wall -Wextra)
    endif()
  endforeach()
endif()
