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
      COMMAND ${GT4_GLSLANG} -V --target-env ${SHADER_ENV} -S ${stage} ${defines}
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
# The native renderer (docs/NATIVE_RENDERER.md).
gt4_shader(gsn_vert ${S}/gsn.vert vert)
gt4_shader(gsn_frag ${S}/gsn.frag frag)
gt4_shader(gsn_frag_export ${S}/gsn.frag frag DEFINES GSN_STENCIL_EXPORT=1)
gt4_shader(gsn_display_frag ${S}/gsn_display.frag frag)
gt4_shader(gsn_stencil_frag ${S}/gsn_stencil.frag frag)
gt4_shader(gsn_reinterpret_comp ${S}/gsn_reinterpret.comp comp)
gt4_shader(gsn_models_comp ${S}/gsn_models.comp comp)
gt4_shader(gsn_grade_comp ${S}/gsn_grade.comp comp)
gt4_shader(gsn_ao_comp ${S}/gsn_ao.comp comp)
gt4_shader(gsn_light_comp ${S}/gsn_light.comp comp)
# Night lighting on the GS path (PLAN.md F2): the albedo estimate's local luminance and the cluster build (F1).
gt4_shader(gsn_night_lum_comp ${S}/gsn_night_lum.comp comp)
gt4_shader(lights_cluster_comp ${S}/lights_cluster.comp comp)
gt4_shader(gsn_velocity_vert ${S}/gsn_velocity.vert vert)
gt4_shader(gsn_velocity_frag ${S}/gsn_velocity.frag frag)
gt4_shader(gsn_taa_comp ${S}/gsn_taa.comp comp)
gt4_shader(gsn_ssr_comp ${S}/gsn_ssr.comp comp)
gt4_shader(gsn_ssr_composite_comp ${S}/gsn_ssr_composite.comp comp)
gt4_shader(gsn_fog_comp ${S}/gsn_fog.comp comp)
gt4_shader(gsn_motionblur_comp ${S}/gsn_motionblur.comp comp)
gt4_shader(gsn_dof_comp ${S}/gsn_dof.comp comp)
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
target_link_libraries(gt4_fsr2 PRIVATE ${GT4_VOLK_HEADERS})
if(MSVC)
  target_compile_options(gt4_fsr2 PRIVATE /W0 /FI${CMAKE_CURRENT_SOURCE_DIR}/platform/render/ffx_compat.h)
else()
  target_compile_options(gt4_fsr2 PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/platform/render/ffx_compat.h)
endif()

# PS2 graphics (docs/GS_BACKEND.md). gt4_ps2gfx is plain C++ (no GPU): the game link and capture
# replay, the graphics stream, VIF1 decoding into batches and GT4's 3D renderers (platform/gs/renderers3d.cpp:
# native code giving primitives to the front end; no VU1 model, docs/NATIVE_ONLY.md). gt4_gs adds the host that
# runs them on a worker thread into the native renderer's front end (gt4_gsnative); the renderer's back end is
# part of gt4_host. GT4_VU1_INTERPRETER adds the VU1 reference (tools/vu1ref: a VU1 memory model, the
# interpreters and the bit-exact port of GT4's programs), development and test tools only: GT4_VU1=exact|interp|ref,
# GT4_VU1_DIFF, GT4_VU1_INVENTORY and the tests that check the native renderers against them.
option(GT4_VU1_INTERPRETER "Build the VU1 reference tools in (development and test builds)" OFF)
find_package(Threads REQUIRED)
add_library(gt4_ps2gfx STATIC
    platform/gs/vif1.cpp
    platform/gs/renderers3d.cpp
    platform/gs/stream.cpp
    platform/gs/link_host.cpp
    platform/scene/scene_decoder.cpp)
if(GT4_VU1_INTERPRETER)
  target_sources(gt4_ps2gfx PRIVATE tools/vu1ref/vu1.cpp tools/vu1ref/vu1_exact.cpp tools/vu1ref/vu1_ref.cpp
      tools/vu1ref/vu1_inventory.cpp tools/vu1ref/vu1_reference.cpp)
  target_include_directories(gt4_ps2gfx PUBLIC tools/vu1ref)
endif()
target_include_directories(gt4_ps2gfx PUBLIC platform/gs platform/scene)
target_link_libraries(gt4_ps2gfx PUBLIC gt4_guest_abi Threads::Threads)
target_compile_definitions(gt4_ps2gfx PUBLIC GT4_VU1_INTERPRETER=$<BOOL:${GT4_VU1_INTERPRETER}>
    $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)
# The native renderer (docs/NATIVE_RENDERER.md): the GS memory model and front end on the CPU.
add_library(gt4_gsnative STATIC platform/gsnative/gs_memory.cpp platform/gsnative/gs_frontend.cpp
    platform/gsnative/texture_name.cpp platform/gsnative/texture_pack.cpp)
target_include_directories(gt4_gsnative PUBLIC platform/gsnative platform/scene include)   # (scene_frame.h: FrameList::scene, A10)
# stb_image and xxHash (texture packs: their PNG files, their names)
target_include_directories(gt4_gsnative PRIVATE third_party/stb third_party/xxhash)
target_link_libraries(gt4_gsnative PUBLIC Threads::Threads gt4_mesh_cache)   # (B6: the scene cache, called at each ViewEnd)
if(MSVC)
  target_compile_options(gt4_gsnative PRIVATE /W4)
else()
  target_compile_options(gt4_gsnative PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_library(gt4_gs STATIC platform/gs/gs_host.cpp)
target_include_directories(gt4_gs PUBLIC platform/gs)
target_link_libraries(gt4_gs PUBLIC gt4_ps2gfx gt4_gsnative PRIVATE gt4_lighting)   # (F2b's glow classifier)
if(MSVC)
  target_compile_options(gt4_ps2gfx PRIVATE /W4)
  target_compile_options(gt4_gs PRIVATE /W4)
else()
  target_compile_options(gt4_ps2gfx PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(gt4_gs PRIVATE -Wall -Wextra -Wpedantic)
endif()
# GT4_GS_REFERENCE (cmake/HostDependencies.cmake): paraLLEl-GS behind gs_backend.h, for the tests that check
# the native renderer against it. Nothing in gt4_host links it.
if(GT4_GS_REFERENCE)
  add_library(gt4_gs_reference STATIC platform/gs/gs_backend.cpp)
  target_include_directories(gt4_gs_reference PUBLIC platform/gs)
  target_link_libraries(gt4_gs_reference PUBLIC gt4_ps2gfx PRIVATE parallel-gs)
  if(MSVC)
    target_compile_options(gt4_gs_reference PRIVATE /W3 /wd4267 /wd4244)
  endif()
endif()

# Saves from a PS2 memory card or export into the native save folder (docs/SAVES.md): plain C++, no GPU.
# The scene cache (platform/scene, docs/modern/ARCHITECTURE.md 3.1): GT4's model batches decoded into object-space
# meshes on worker threads (ticket B5). Plain C++, no GPU.
add_library(gt4_mesh_cache STATIC platform/scene/mesh_cache.cpp platform/scene/material_table.cpp platform/scene/scene_cache.cpp)
target_include_directories(gt4_mesh_cache PUBLIC platform/scene include PRIVATE third_party)
target_compile_features(gt4_mesh_cache PUBLIC cxx_std_20)
target_link_libraries(gt4_mesh_cache PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(gt4_mesh_cache PRIVATE /W4)
else()
  target_compile_options(gt4_mesh_cache PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The lighting's CPU parts (platform/render/modern, tickets F2 and F2b): the light sources' defaults, the gather into
# the light list, the course lamps from the meshes and their glow class. No GPU; the host and the tests link it, so
# each is compiled once.
add_library(gt4_lighting STATIC platform/render/modern/light_sources.cpp platform/render/modern/lights_gather.cpp
    platform/render/modern/course_lamps.cpp platform/render/modern/course_lamp_pass.cpp
    platform/render/modern/glow_classifier.cpp)
target_include_directories(gt4_lighting PUBLIC platform/render/modern include)
target_link_libraries(gt4_lighting PUBLIC gt4_mesh_cache)
target_compile_definitions(gt4_lighting PRIVATE $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)
if(MSVC)
  target_compile_options(gt4_lighting PRIVATE /W4)
else()
  target_compile_options(gt4_lighting PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(gt4_saves STATIC platform/saves/save_import.cpp)
target_include_directories(gt4_saves PUBLIC platform/saves)
target_compile_features(gt4_saves PUBLIC cxx_std_20)

# The SPU2 (platform/audio/spu; PLAN.md D5): one source with the game's runtime, which answers GT4's status from it.
add_library(gt4_spu STATIC platform/audio/spu/spu2.cpp)
target_include_directories(gt4_spu PUBLIC platform/audio)
target_compile_features(gt4_spu PUBLIC cxx_std_20)

# The game's pads from the keyboard and gamepads through an action map and the sticks' response (platform/input; no SDL
# calls, its headers).
add_library(gt4_input STATIC platform/input/input_map.cpp platform/input/response.cpp platform/input/devices.cpp
    platform/input/haptics.cpp)
target_include_directories(gt4_input PUBLIC platform/input platform/gs include)
target_link_libraries(gt4_input PUBLIC SDL3::Headers)
target_compile_features(gt4_input PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(gt4_input PRIVATE /W4)
else()
  target_compile_options(gt4_input PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The settings: the shared table and each module's options (platform/sdl/settings_table.h).
set(GT4_SETTINGS_SOURCES
    platform/sdl/settings.cpp
    platform/sdl/settings_display.cpp
    platform/sdl/settings_render.cpp
    platform/sdl/settings_scene.cpp
    platform/sdl/settings_effects.cpp
    platform/sdl/settings_lighting.cpp
    platform/sdl/settings_input.cpp
    platform/sdl/settings_audio.cpp)

add_executable(gt4_host
    platform/sdl/main.cpp
    platform/sdl/disc_setup.cpp
    platform/sdl/renderer.cpp
    platform/sdl/session.cpp
    platform/sdl/pad_devices.cpp
    platform/sdl/device_settings.cpp
    ${GT4_SETTINGS_SOURCES}
    platform/sdl/settings_menu.cpp
    platform/sdl/display.cpp
    platform/render/vk.cpp
    platform/render/pipeline_cache_file.cpp
    platform/render/graph/render_graph.cpp
    platform/render/pipeline.cpp
    platform/render/display_feature.cpp
    platform/render/test_scene.cpp
    platform/render/gsn_scene.cpp
    platform/render/modern/lights_cluster.cpp
    platform/render/overlay.cpp
    platform/render/fsr2.cpp
    platform/render/smaa_textures.cpp
    ${GT4_SHADER_OUT}/gt4_shaders.cpp
    ${GT4_SHADER_HEADERS})
target_include_directories(gt4_host PRIVATE platform/sdl platform/render platform/render/graph platform/render/modern
    ${GT4_SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${GT4_FSR2_DIR})
target_compile_definitions(gt4_host PRIVATE VK_NO_PROTOTYPES FFX_GCC GT4_LIGHTS_CLUSTER_GPU=1)
target_link_libraries(gt4_host PRIVATE SDL3::SDL3 Vulkan::Headers gt4_guest_abi gt4_fsr2 gt4_gs gt4_saves gt4_input gt4_lighting ${GT4_VOLK} ${GT4_VOLK_HEADERS})
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
  # A device's own input settings (PLAN.md D13): which, their check, the order and the gates, the file's round trip.
  add_executable(gt4_device_settings_test tests/device_settings.cpp platform/sdl/device_settings.cpp ${GT4_SETTINGS_SOURCES})
  target_include_directories(gt4_device_settings_test PRIVATE platform/sdl platform/render platform/gs)
  target_link_libraries(gt4_device_settings_test PRIVATE gt4_input)
  add_test(NAME device_settings COMMAND gt4_device_settings_test)

  add_executable(gt4_host_settings_test tests/host_settings.cpp ${GT4_SETTINGS_SOURCES} platform/sdl/settings_menu.cpp)
  target_include_directories(gt4_host_settings_test PRIVATE platform/sdl platform/render platform/gs)
  if(MSVC)
    target_compile_options(gt4_host_settings_test PRIVATE /W4)
  else()
    target_compile_options(gt4_host_settings_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME host_settings COMMAND gt4_host_settings_test)

  # The settings menu's page engine and layout (platform/ui, MENU_DESIGN.md M2): golden draw lists in tests/ui.
  add_executable(gt4_ui_menu_test tests/ui_menu.cpp platform/ui/menu_model.cpp platform/ui/menu_layout.cpp
                 platform/ui/draw_list.cpp)
  target_include_directories(gt4_ui_menu_test PRIVATE platform/ui)
  if(MSVC)
    target_compile_options(gt4_ui_menu_test PRIVATE /W4)
  else()
    target_compile_options(gt4_ui_menu_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ui_menu COMMAND gt4_ui_menu_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui)

  # The settings pages' link records (include/gt4/ui.h, platform/ui/ui_records, MENU_DESIGN.md M1).
  add_executable(gt4_ui_records_test tests/ui_records.cpp platform/ui/ui_records.cpp platform/ui/draw_list.cpp)
  target_include_directories(gt4_ui_records_test PRIVATE platform/ui include)
  if(MSVC)
    target_compile_options(gt4_ui_records_test PRIVATE /W4)
  else()
    target_compile_options(gt4_ui_records_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ui_records COMMAND gt4_ui_records_test)

  # The pipeline cache's file: Vulkan's header checked against the GPU, the save replacing the old file (no GPU).
  add_executable(gt4_pipeline_cache_file_test tests/pipeline_cache_file.cpp platform/render/pipeline_cache_file.cpp)
  target_include_directories(gt4_pipeline_cache_file_test PRIVATE platform/render third_party)
  if(MSVC)
    target_compile_options(gt4_pipeline_cache_file_test PRIVATE /W4 /utf-8)
  else()
    target_compile_options(gt4_pipeline_cache_file_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pipeline_cache_file COMMAND gt4_pipeline_cache_file_test ${CMAKE_CURRENT_BINARY_DIR}/pipeline_cache_file_test)

  # The mesh cache against the CPU 3D renderers on captures (game data: GT4_MESH_CAPTURES, by default the race7, race and
  # event captures in build/caps; skipped (77) without them).
  set(gt4_caps ${CMAKE_SOURCE_DIR}/build/caps)
  set(GT4_MESH_CAPTURES "${gt4_caps}/race7.cap;${gt4_caps}/race.cap;${gt4_caps}/event.cap" CACHE STRING
      "Captures the mesh_cache test replays")
  add_executable(gt4_mesh_cache_test tests/mesh_cache.cpp)
  target_link_libraries(gt4_mesh_cache_test PRIVATE gt4_mesh_cache gt4_ps2gfx)
  add_test(NAME mesh_cache COMMAND gt4_mesh_cache_test ${GT4_MESH_CAPTURES})
add_executable(gt4_material_table_test tests/material_table.cpp)
target_link_libraries(gt4_material_table_test PRIVATE gt4_mesh_cache)
add_test(NAME material_table COMMAND gt4_material_table_test)
  set_tests_properties(mesh_cache PROPERTIES SKIP_RETURN_CODE 77)

  # The scene records (docs/modern/CONTRACTS.md (a)): gfx.h's layouts against the game's C copy (compat/game/
  # gt4_scene.h), and a kScene record through the graphics stream to GsTarget::scene.
  add_executable(gt4_scene_records_test tests/scene_records.cpp)
  target_include_directories(gt4_scene_records_test PRIVATE compat/game)
  target_link_libraries(gt4_scene_records_test PRIVATE gt4_ps2gfx)
  add_test(NAME scene_records COMMAND gt4_scene_records_test)

  # The scene decoder (platform/scene/scene_decoder.h, CONTRACTS.md a.5, a.7): synthetic record streams, one test per
  # counter; a well-formed frame leaves every error counter at 0.
  add_executable(gt4_scene_decoder_test tests/scene_decoder.cpp)
  target_link_libraries(gt4_scene_decoder_test PRIVATE gt4_ps2gfx)
  add_test(NAME scene_decoder COMMAND gt4_scene_decoder_test)

  # The render graph (platform/render/graph): compile on the CPU with a fake backend (no GPU), and a few frames
  # on a GPU with validation and synchronization validation when the layer is installed (skipped (77) without one).
  add_library(gt4_render_graph_testlib STATIC platform/render/graph/render_graph.cpp platform/render/vk.cpp
      platform/render/pipeline_cache_file.cpp)
  target_include_directories(gt4_render_graph_testlib PUBLIC platform/render platform/render/graph platform/gs third_party)
  target_compile_definitions(gt4_render_graph_testlib PUBLIC VK_NO_PROTOTYPES $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)
  target_link_libraries(gt4_render_graph_testlib PUBLIC Vulkan::Headers ${GT4_VOLK} ${GT4_VOLK_HEADERS})
  add_executable(gt4_render_graph_test tests/render_graph.cpp)
  add_executable(gt4_render_graph_gpu_test tests/render_graph_gpu.cpp)
  foreach(t gt4_render_graph_testlib gt4_render_graph_test gt4_render_graph_gpu_test)
    if(MSVC)
      target_compile_options(${t} PRIVATE /W4)
    else()
      target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endforeach()
  target_link_libraries(gt4_render_graph_test PRIVATE gt4_render_graph_testlib)
  target_link_libraries(gt4_render_graph_gpu_test PRIVATE gt4_render_graph_testlib)
  add_test(NAME render_graph COMMAND gt4_render_graph_test)
  add_test(NAME render_graph_gpu COMMAND gt4_render_graph_gpu_test)
  set_tests_properties(render_graph_gpu PROPERTIES SKIP_RETURN_CODE 77)

  # Saves from memory card images, .psu exports and folder cards (built here; no game data).
  add_executable(gt4_save_import_test tests/save_import.cpp)
  target_link_libraries(gt4_save_import_test PRIVATE gt4_saves)
  add_test(NAME save_import COMMAND gt4_save_import_test)

  # The SPU2 (platform/audio/spu): a voice's timing the same mixed or only advanced (the game's status and the host's
  # voices agree, PLAN.md D5), ADPCM, the envelope, loops and ends, key-off, the status (built here; no game data).
  add_executable(gt4_spu2_test tests/spu2.cpp)
  target_link_libraries(gt4_spu2_test PRIVATE gt4_spu)
  if(MSVC)
    target_compile_options(gt4_spu2_test PRIVATE /W4)
  else()
    target_compile_options(gt4_spu2_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME spu2 COMMAND gt4_spu2_test)

  # The input action map: the original map against the host's mapping before it, for every input.
  add_executable(gt4_input_map_test tests/input_map.cpp)
  target_link_libraries(gt4_input_map_test PRIVATE gt4_input)
  add_test(NAME input_map COMMAND gt4_input_map_test)

  # The sticks' and pedals' response (PLAN.md D12): the shaping's edges, monotonic, full lock, smoothing's time
  # constant, the keys' ramp, and original untouched.
  add_executable(gt4_input_response_test tests/input_response.cpp)
  target_link_libraries(gt4_input_response_test PRIVATE gt4_input)
  add_test(NAME input_response COMMAND gt4_input_response_test)

  # The input devices (PLAN.md D13): the default profiles are the original map, profiles round-trip, the refused
  # remaps are refused, ports stay with their devices across reconnects.
  add_executable(gt4_input_devices_test tests/input_devices.cpp)
  target_link_libraries(gt4_input_devices_test PRIVATE gt4_input)
  add_test(NAME input_devices COMMAND gt4_input_devices_test)

  # Gamepad haptics from the physics (PLAN.md D14): a car's Vehicle record to motors and triggers, frame by frame.
  add_executable(gt4_input_haptics_test tests/input_haptics.cpp)
  target_link_libraries(gt4_input_haptics_test PRIVATE gt4_input)
  add_test(NAME input_haptics COMMAND gt4_input_haptics_test)

  # The cards behind the game's libmc calls (runtime/save_folder.cpp, built here on its own): cut writes,
  # hidden copies and backups, backup rotation, the second slot. Works in the build folder.
  add_executable(gt4_memcard_test tests/memcard.cpp runtime/save_folder.cpp)
  target_include_directories(gt4_memcard_test PRIVATE runtime)
  target_compile_features(gt4_memcard_test PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(gt4_memcard_test PRIVATE /W4)
  else()
    target_compile_options(gt4_memcard_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME memcard COMMAND gt4_memcard_test ${CMAKE_CURRENT_BINARY_DIR}/memcard_test)
  # D6: a car's bytes as GT4 keeps them give the scene::Vehicle record (runtime/vehicle_decode.cpp).
  add_executable(gt4_vehicle_test tests/vehicle.cpp runtime/vehicle_decode.cpp)
  target_include_directories(gt4_vehicle_test PRIVATE runtime include)
  target_compile_features(gt4_vehicle_test PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(gt4_vehicle_test PRIVATE /W4)
  else()
    target_compile_options(gt4_vehicle_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME vehicle COMMAND gt4_vehicle_test)
  # input.trigger_travel = full: the game's pedal pressures rescaled (runtime/pad_pedals.h, header only).
  add_executable(gt4_pad_pedals_test tests/pad_pedals.cpp)
  target_include_directories(gt4_pad_pedals_test PRIVATE runtime)
  target_compile_features(gt4_pad_pedals_test PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(gt4_pad_pedals_test PRIVATE /W4)
  else()
    target_compile_options(gt4_pad_pedals_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pad_pedals COMMAND gt4_pad_pedals_test)
  # input.steer.game_band: GT4's steering band compensated in the left stick's byte (runtime/pad_steer.h, header only).
  add_executable(gt4_pad_steer_test tests/pad_steer.cpp)
  target_include_directories(gt4_pad_steer_test PRIVATE runtime)
  target_compile_features(gt4_pad_steer_test PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(gt4_pad_steer_test PRIVATE /W4)
  else()
    target_compile_options(gt4_pad_steer_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pad_steer COMMAND gt4_pad_steer_test)

  # PS2 graphics on the CPU: VIF1, GT4's 3D renderers, stream records, replay, pad (no GPU needed).
  add_executable(gt4_ps2gfx_test tests/ps2gfx.cpp)
  target_link_libraries(gt4_ps2gfx_test PRIVATE gt4_ps2gfx)
  add_test(NAME ps2gfx COMMAND gt4_ps2gfx_test)
  # The native 3D renderers and the bit-exact port against the reference interpreter on random batches
  # (GT4_VU1_INTERPRETER builds); needs GT4's micro image (game data, GT4_VU1_IMAGE=path), skipped (77) without.
  set(gt4_vu1_tests)
  if(GT4_VU1_INTERPRETER)
    add_executable(gt4_vu1_native_fuzz tests/vu1_native_fuzz.cpp)
    target_link_libraries(gt4_vu1_native_fuzz PRIVATE gt4_ps2gfx)
    add_test(NAME vu1_native_fuzz COMMAND gt4_vu1_native_fuzz)
    set_tests_properties(vu1_native_fuzz PROPERTIES SKIP_RETURN_CODE 77)
    set(gt4_vu1_tests gt4_vu1_native_fuzz)
  endif()
  # The graphics host on the CPU (events, reads, a capture replayed into frame lists), and that capture drawn
  # by gt4_host (headless, "original" preset, compared by pixel; needs a Vulkan GPU, skipped (77) without).
  # Outputs go to the build directory.
  # The POSIX game link (link_host.cpp: a /dev/shm file, the game started through GT4_GAME_LAUNCHER) against a fake
  # game in a child process: every record across a ring wrap, the quit, the exit code, the file removed (PLAN.md C9).
  if(UNIX)
    add_executable(gt4_link_posix_test tests/link_posix.cpp)
    target_link_libraries(gt4_link_posix_test PRIVATE gt4_gs)
    add_test(NAME link_posix COMMAND gt4_link_posix_test)
  endif()
  add_executable(gt4_gs_host_test tests/gs_host.cpp)
  target_link_libraries(gt4_gs_host_test PRIVATE gt4_gs)
  set(GT4_GS_TEST_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gs_test_output)
  add_test(NAME gs_host COMMAND gt4_gs_host_test ${GT4_GS_TEST_OUTPUT})
  set_tests_properties(gs_host PROPERTIES FIXTURES_SETUP gs_replay_capture)
  add_test(NAME host_replay COMMAND ${CMAKE_COMMAND} -DHOST=$<TARGET_FILE:gt4_host>
      -DCHECKER=$<TARGET_FILE:gt4_gs_host_test> -DOUTPUT=${GT4_GS_TEST_OUTPUT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/host_replay.cmake)
  set_tests_properties(host_replay PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED gs_replay_capture)
  set(gt4_gs_reference_tests)
  if(GT4_GS_REFERENCE)
    # paraLLEl-GS checked by pixel, and the native renderer's GS memory model against it: need a Vulkan GPU
    # that meets paraLLEl-GS's requirements; skipped (exit code 77) where there is none.
    add_executable(gt4_gs_backend_test tests/gs_backend.cpp)
    target_link_libraries(gt4_gs_backend_test PRIVATE gt4_gs_reference)
    add_test(NAME gs_backend COMMAND gt4_gs_backend_test)
    set_tests_properties(gs_backend PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(gt4_gs_native_test tests/gs_native.cpp)
    target_link_libraries(gt4_gs_native_test PRIVATE gt4_gs_reference gt4_gsnative)
    add_test(NAME gs_native COMMAND gt4_gs_native_test)
    set_tests_properties(gs_native PROPERTIES SKIP_RETURN_CODE 77)
    set(gt4_gs_reference_tests gt4_gs_backend_test gt4_gs_native_test)
  endif()
  # The GS memory model's fast paths (palette loads, uploads, decodes) against its pixel-by-pixel reference.
  add_executable(gt4_gs_memory_test tests/gs_memory.cpp)
  target_link_libraries(gt4_gs_memory_test PRIVATE gt4_gsnative)
  add_test(NAME gs_memory COMMAND gt4_gs_memory_test)
  # The light list contract (docs/modern/CONTRACTS.md (c)): the C++ and GLSL layouts and functions agree, checked
  # by a compute shader that is not part of the host. Needs a Vulkan GPU; skipped (77) without one.
  add_custom_command(OUTPUT ${GT4_SHADER_OUT}/spv_light_list_test.h
      COMMAND ${GT4_GLSLANG} -V --target-env vulkan1.2 -S comp -I${GT4_SHADER_SOURCE}
              --vn spv_light_list_test -o ${GT4_SHADER_OUT}/spv_light_list_test.h
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/light_list_test.comp
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/light_list_test.comp ${GT4_SHADER_SOURCE}/light_list.glsl
      COMMENT "Compiling shader light_list_test")
  add_executable(gt4_light_list_test tests/light_list.cpp ${GT4_SHADER_OUT}/spv_light_list_test.h)
  target_include_directories(gt4_light_list_test PRIVATE ${GT4_SHADER_OUT} ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(gt4_light_list_test PRIVATE VK_NO_PROTOTYPES)
  target_link_libraries(gt4_light_list_test PRIVATE Vulkan::Headers ${GT4_VOLK} ${GT4_VOLK_HEADERS})
  if(UNIX AND NOT APPLE)
    target_link_libraries(gt4_light_list_test PRIVATE ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME light_list COMMAND gt4_light_list_test)
  set_tests_properties(light_list PROPERTIES SKIP_RETURN_CODE 77)
  # The cluster build (ticket F1): GPU result against the CPU reference, with the host's shader (gt4_shader
  # lights_cluster_comp, as F2's night lighting runs it). Needs a Vulkan 1.3 GPU; skipped (77) without.
  add_executable(gt4_lights_cluster_test tests/lights_cluster.cpp platform/render/modern/lights_cluster.cpp
      platform/render/vk.cpp platform/render/pipeline_cache_file.cpp ${GT4_SHADER_OUT}/spv_lights_cluster_comp.h)
  target_include_directories(gt4_lights_cluster_test PRIVATE ${GT4_SHADER_OUT} ${CMAKE_CURRENT_SOURCE_DIR}/include
      platform/render platform/render/modern third_party)
  target_compile_definitions(gt4_lights_cluster_test PRIVATE VK_NO_PROTOTYPES GT4_LIGHTS_CLUSTER_GPU=1)
  target_link_libraries(gt4_lights_cluster_test PRIVATE Vulkan::Headers ${GT4_VOLK} ${GT4_VOLK_HEADERS})
  if(UNIX AND NOT APPLE)
    target_link_libraries(gt4_lights_cluster_test PRIVATE ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME lights_cluster COMMAND gt4_lights_cluster_test)
  set_tests_properties(lights_cluster PROPERTIES SKIP_RETURN_CODE 77)
  # The light gather (ticket F2): Light records into the light list, CPU only.
  # The lighting's CPU parts (gt4_lighting), each test CPU only: the light gather (F2), the course's lamps from a
  # mesh item (F2b), and F2b's glow class and lamp pass on a synthetic scene frame.
  foreach(test IN ITEMS lights_gather course_lamps course_lamp_pass)
    add_executable(gt4_${test}_test tests/${test}.cpp)
    target_link_libraries(gt4_${test}_test PRIVATE gt4_lighting)
    target_compile_definitions(gt4_${test}_test PRIVATE $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)
    if(MSVC)
      target_compile_options(gt4_${test}_test PRIVATE /W4)
    else()
      target_compile_options(gt4_${test}_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${test} COMMAND gt4_${test}_test)
  endforeach()
  # The night artefact metrics (tools/frame_metrics.py, ticket N1) on synthetic frames; skipped (77) without
  # Python 3 and numpy.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME frame_metrics_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/frame_metrics.py --selftest)
    set_tests_properties(frame_metrics_selftest PROPERTIES SKIP_RETURN_CODE 77)
    # The frame comparison tool (tools/frame_diff.py, ticket C1) on synthetic frames, with and without numpy.
    add_test(NAME frame_diff_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/frame_diff.py --selftest)
    # The machine's slots (tools/slot.py): a live holder kept, a dead or reused one removed, on a temporary folder.
    add_test(NAME slot_selftest COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/slot.py --self-test)
    # A replay comes out the same every run with frame interpolation and the HD pack on (C1): a capture with 3D
    # replayed twice, every frame bit for bit. Needs GT4_DETERMINISM_CAPTURE (captures are not in the repository)
    # and a Vulkan GPU; skipped (77) without the capture.
    add_test(NAME replay_determinism
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_determinism.py $<TARGET_FILE:gt4_host>)
    set_tests_properties(replay_determinism PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  # The game runtime's whole-range test for game memory (runtime/guest.h).
  add_executable(gt4_guest_ram_test tests/guest_ram.cpp)
  add_test(NAME guest_ram COMMAND gt4_guest_ram_test)
  foreach(test_target IN ITEMS gt4_ps2gfx_test gt4_gs_host_test gt4_gs_memory_test gt4_guest_ram_test gt4_light_list_test gt4_lights_cluster_test gt4_lights_gather_test ${gt4_vu1_tests} ${gt4_gs_reference_tests})
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG /W4)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG -Wall -Wextra)
    endif()
  endforeach()
endif()

