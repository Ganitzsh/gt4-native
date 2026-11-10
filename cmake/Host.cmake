# gt4_host targets: shaders compiled to SPIR-V by glslang at build time and embedded as C++ arrays,
# AMD FidelityFX FSR 2 (vendored, MIT) and the host executable.

set(GT4_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/platform/render/shaders)
set(GT4_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/gt4_shaders)
set(GT4_FSR2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fidelityfx-fsr2)
set(GT4_FSR3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fidelityfx-sdk)
file(MAKE_DIRECTORY ${GT4_SHADER_OUT})
file(GLOB GT4_SHADER_INCLUDES CONFIGURE_DEPENDS
    ${GT4_SHADER_SOURCE}/*.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl
    ${GT4_FSR2_DIR}/shaders/*.h)
file(GLOB_RECURSE GT4_FSR3_SHADER_INCLUDES CONFIGURE_DEPENDS ${GT4_FSR3_DIR}/include/FidelityFX/gpu/*.h)
set(GT4_SHADER_NAMES "")
set(GT4_SHADER_HEADERS "")

# gt4_shader(<name> <source> <stage> [ENV vulkan1.x] [DEFINES A=1 B ...] [INCLUDES dir ...])
function(gt4_shader name source stage)
  cmake_parse_arguments(PARSE_ARGV 3 SHADER "" "ENV" "DEFINES;INCLUDES")
  if(NOT SHADER_ENV)
    set(SHADER_ENV vulkan1.3)
  endif()
  set(defines "")
  foreach(define IN LISTS SHADER_DEFINES)
    list(APPEND defines "-D${define}")
  endforeach()
  set(includes "")
  foreach(dir IN LISTS SHADER_INCLUDES)
    list(APPEND includes "-I${dir}")
  endforeach()
  set(output ${GT4_SHADER_OUT}/spv_${name}.h)
  add_custom_command(OUTPUT ${output}
      COMMAND ${GT4_GLSLANG} -V --target-env ${SHADER_ENV} -S ${stage} ${defines}
              -I${GT4_SHADER_SOURCE} -I${CMAKE_CURRENT_SOURCE_DIR}/third_party ${includes}
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
# B24 (GT4_GS_EXACT): GS-exact texel coordinates as their own variants, so the default SPIR-V stays main's to the byte.
gt4_shader(gsn_vert_exact ${S}/gsn.vert vert DEFINES GSN_EXACT=1)
gt4_shader(gsn_frag_exact ${S}/gsn.frag frag DEFINES GSN_EXACT=1)
gt4_shader(gsn_frag_export_exact ${S}/gsn.frag frag DEFINES GSN_EXACT=1 GSN_STENCIL_EXPORT=1)
# B7's parity path (render.scene = parity): a view's items from the mesh cache, through gsn.frag's GSN_PARITY variants.
gt4_shader(modern_parity_vert ${S}/modern_parity.vert vert)
gt4_shader(gsn_frag_parity ${S}/gsn.frag frag DEFINES GSN_PARITY=1)
gt4_shader(gsn_frag_parity_export ${S}/gsn.frag frag DEFINES GSN_PARITY=1 GSN_STENCIL_EXPORT=1)
# B8's side channels (render.scene = hybrid): the items redrawn per depth partition, and the partition's reset.
gt4_shader(modern_side_vert ${S}/modern_parity.vert vert DEFINES GSN_SIDE=1)
gt4_shader(modern_side_frag ${S}/modern_side.frag frag)
gt4_shader(gsn_side_reset_vert ${S}/gsn_side_reset.vert vert)
gt4_shader(gsn_side_reset_frag ${S}/gsn_side_reset.frag frag)
gt4_shader(gsn_side_view_frag ${S}/gsn_side_view.frag frag)
gt4_shader(gsn_side_veil_frag ${S}/gsn_side_veil.frag frag)
gt4_shader(gsn_display_frag ${S}/gsn_display.frag frag)
gt4_shader(gsn_display_seam_frag ${S}/gsn_display.frag frag DEFINES GSN_SEAM=1)
gt4_shader(gsn_seam_comp ${S}/gsn_seam.comp comp)
gt4_shader(gsn_seam_keep_comp ${S}/gsn_seam_keep.comp comp)
gt4_shader(gsn_stencil_frag ${S}/gsn_stencil.frag frag)
gt4_shader(gsn_reinterpret_comp ${S}/gsn_reinterpret.comp comp)
gt4_shader(gsn_models_comp ${S}/gsn_models.comp comp)
gt4_shader(gsn_grade_comp ${S}/gsn_grade.comp comp)
gt4_shader(gsn_ao_comp ${S}/gsn_ao.comp comp)
gt4_shader(gsn_light_comp ${S}/gsn_light.comp comp)
# Night lighting on the GS path (PLAN.md F2): the albedo estimate's local luminance and the cluster build (F1).
gt4_shader(gsn_night_lum_comp ${S}/gsn_night_lum.comp comp)
gt4_shader(gsn_mask_frag ${S}/gsn_mask.frag frag)
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
gt4_shader(overlay_sdf_frag ${S}/overlay_sdf.frag frag)

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

# FSR 3.1's upscaler passes (third_party/fidelityfx-sdk, PLAN.md B17), compiled from the vendored GLSL as published
# with the inputs this host gives (HDR colour, render-resolution unjittered motion vectors, inverted depth) and the
# reference reprojection. Each in 32 and 16-bit arithmetic (_h0, _h1; FFX_HALF): platform/render/upscale/
# fsr3_shaders.cpp serves the 16-bit ones to a device that has them; RCAS is 32-bit only, as the SDK asks it. These
# replace the SDK's generated permutation tables. They get their own table in gt4_fsr3 (below), so a binary holds them
# only when it links FSR 3. (Nothing calls FSR 3 yet: B17 step 2.)
set(GT4_SHADER_NAMES_BEFORE_FSR3 ${GT4_SHADER_NAMES})
set(GT4_SHADER_HEADERS_BEFORE_FSR3 ${GT4_SHADER_HEADERS})
set(GT4_FSR3_DEFINES FFX_GLSL=1 FFX_GPU=1 FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=1
    FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1 FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0
    FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=1 FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=0)
set(GT4_FSR3_PASSES ${GT4_FSR3_DIR}/src/backends/vk/shaders/fsr3upscaler)
# The SDK's GLSL declares the luma history rgba8, but the SDK makes it R16G16B16A16_FLOAT (ffx_fsr3upscaler.cpp's
# LumaHistory1/2), so every load and store through it is undefined (the validation layer: B17 step 2's VAL; the
# HLSL path names no format). The vendored header stays as it is: a copy with that one declaration corrected is
# written here and found first, beside its own folder for the copy's relative includes. Configure stops if the line
# isn't there exactly once (a later SDK: check whether it's still needed).
set(GT4_FSR3_FIX ${GT4_SHADER_OUT}/fsr3_fix)
set(GT4_FSR3_GPU ${GT4_FSR3_DIR}/include/FidelityFX/gpu)
file(READ ${GT4_FSR3_GPU}/fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h GT4_FSR3_CALLBACKS)
set(GT4_FSR3_WRONG "binding = FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba8)")
string(REPLACE "${GT4_FSR3_WRONG}" "" GT4_FSR3_WITHOUT "${GT4_FSR3_CALLBACKS}")
string(LENGTH "${GT4_FSR3_CALLBACKS}" GT4_FSR3_LENGTH_A)
string(LENGTH "${GT4_FSR3_WITHOUT}" GT4_FSR3_LENGTH_B)
string(LENGTH "${GT4_FSR3_WRONG}" GT4_FSR3_LENGTH_W)
math(EXPR GT4_FSR3_FOUND "(${GT4_FSR3_LENGTH_A} - ${GT4_FSR3_LENGTH_B}) / ${GT4_FSR3_LENGTH_W}")
if(NOT GT4_FSR3_FOUND EQUAL 1)
  message(FATAL_ERROR "FSR 3: the luma history's declaration was found ${GT4_FSR3_FOUND} times, not once")
endif()
string(REPLACE "${GT4_FSR3_WRONG}" "binding = FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba16f)" GT4_FSR3_CALLBACKS
       "${GT4_FSR3_CALLBACKS}")
set(GT4_FSR3_FIXED ${GT4_FSR3_FIX}/fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h)
set(GT4_FSR3_OLD "")
if(EXISTS ${GT4_FSR3_FIXED})
  file(READ ${GT4_FSR3_FIXED} GT4_FSR3_OLD)
endif()
if(NOT GT4_FSR3_OLD STREQUAL GT4_FSR3_CALLBACKS)   # (rewritten only when it changes: no needless shader rebuilds)
  file(WRITE ${GT4_FSR3_FIXED} "${GT4_FSR3_CALLBACKS}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${GT4_FSR3_GPU}/fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h)
set(GT4_SHADER_INCLUDES_BEFORE_FSR3 ${GT4_SHADER_INCLUDES})
list(APPEND GT4_SHADER_INCLUDES ${GT4_FSR3_SHADER_INCLUDES} ${GT4_FSR3_FIXED})
foreach(pass prepare_inputs luma_pyramid shading_change_pyramid shading_change prepare_reactivity luma_instability
             debug_view autogen_reactive accumulate)
  foreach(half 0 1)
    gt4_shader(fsr3_${pass}_h${half} ${GT4_FSR3_PASSES}/ffx_fsr3upscaler_${pass}_pass.glsl comp ENV vulkan1.1
        DEFINES ${GT4_FSR3_DEFINES} FFX_HALF=${half} FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=0
        INCLUDES ${GT4_FSR3_FIX} ${GT4_FSR3_GPU} ${GT4_FSR3_GPU}/fsr3upscaler)
  endforeach()
endforeach()
foreach(half 0 1)
  gt4_shader(fsr3_accumulate_sharpen_h${half} ${GT4_FSR3_PASSES}/ffx_fsr3upscaler_accumulate_pass.glsl comp
      ENV vulkan1.1 DEFINES ${GT4_FSR3_DEFINES} FFX_HALF=${half} FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=1
      INCLUDES ${GT4_FSR3_FIX} ${GT4_FSR3_GPU} ${GT4_FSR3_GPU}/fsr3upscaler)
endforeach()
gt4_shader(fsr3_rcas_h0 ${GT4_FSR3_PASSES}/ffx_fsr3upscaler_rcas_pass.glsl comp ENV vulkan1.1
    DEFINES ${GT4_FSR3_DEFINES} FFX_HALF=0 FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=0
    INCLUDES ${GT4_FSR3_FIX} ${GT4_FSR3_GPU} ${GT4_FSR3_GPU}/fsr3upscaler)
set(GT4_SHADER_INCLUDES ${GT4_SHADER_INCLUDES_BEFORE_FSR3})
set(GT4_FSR3_SHADER_NAMES ${GT4_SHADER_NAMES})
list(REMOVE_ITEM GT4_FSR3_SHADER_NAMES ${GT4_SHADER_NAMES_BEFORE_FSR3})
set(GT4_FSR3_SHADER_HEADERS ${GT4_SHADER_HEADERS})
list(REMOVE_ITEM GT4_FSR3_SHADER_HEADERS ${GT4_SHADER_HEADERS_BEFORE_FSR3})
set(GT4_SHADER_NAMES ${GT4_SHADER_NAMES_BEFORE_FSR3})
set(GT4_SHADER_HEADERS ${GT4_SHADER_HEADERS_BEFORE_FSR3})

# One translation unit per table holds its SPIR-V arrays; others call gt4::shaders::<name>() through its header.
function(gt4_shader_table table)
  set(header "#pragma once\n// Generated by cmake/Host.cmake: SPIR-V of host shaders.\n")
  string(APPEND header "#include \"vk.h\"\nnamespace gt4::shaders {\n")
  set(source "// Generated by cmake/Host.cmake.\n#include <cstdint>\n#include \"${table}.h\"\n")
  foreach(name IN LISTS ARGN)
    string(APPEND header "vk::ShaderCode ${name}();\n")
    string(APPEND source "#include \"spv_${name}.h\"\n")
  endforeach()
  string(APPEND header "}  // namespace gt4::shaders\n")
  string(APPEND source "namespace gt4::shaders {\n")
  foreach(name IN LISTS ARGN)
    string(APPEND source "vk::ShaderCode ${name}() { return {spv_${name}, sizeof(spv_${name})}; }\n")
  endforeach()
  string(APPEND source "}  // namespace gt4::shaders\n")
  file(CONFIGURE OUTPUT ${GT4_SHADER_OUT}/${table}.h CONTENT "${header}")
  file(CONFIGURE OUTPUT ${GT4_SHADER_OUT}/${table}.cpp CONTENT "${source}")
endfunction()
gt4_shader_table(gt4_shaders ${GT4_SHADER_NAMES})
gt4_shader_table(gt4_fsr3_shaders ${GT4_FSR3_SHADER_NAMES})

# The UI fonts (assets/ui/fonts, SIL OFL, byte for byte; PLAN.md E5): embedded as arrays by cmake/EmbedFile.cmake,
# so a release needs no assets folder beside gt4_host. platform/ui/host/ui_fonts.h declares the table.
set(GT4_FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/fonts)
set(GT4_FONT_HEADERS "")
set(fonts_source "// Generated by cmake/Host.cmake: the UI fonts (assets/ui/fonts), byte for byte.\n#include \"ui_fonts.h\"\n")
set(fonts_table "")
foreach(font SairaSemiExpanded-BoldItalic SairaSemiExpanded-Regular Saira-Regular LibreCaslonText-Regular)
  string(MAKE_C_IDENTIFIER "font_${font}" id)
  set(output ${GT4_SHADER_OUT}/${id}.h)
  add_custom_command(OUTPUT ${output}
      COMMAND ${CMAKE_COMMAND} -DIN=${GT4_FONT_DIR}/${font}.ttf -DOUT=${output} -DNAME=${id}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
      DEPENDS ${GT4_FONT_DIR}/${font}.ttf ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
      COMMENT "Embedding font ${font}"
      VERBATIM)
  list(APPEND GT4_FONT_HEADERS ${output})
  string(APPEND fonts_source "#include \"${id}.h\"\n")
  string(APPEND fonts_table "    {\"${font}\", ${id}, sizeof(${id})},\n")
endforeach()
string(APPEND fonts_source "namespace gt4::ui {\nconst EmbeddedFont kEmbeddedFonts[] = {\n${fonts_table}};\n"
       "const size_t kEmbeddedFontCount = sizeof(kEmbeddedFonts) / sizeof(kEmbeddedFonts[0]);\n}  // namespace gt4::ui\n")
file(CONFIGURE OUTPUT ${GT4_SHADER_OUT}/ui_fonts.cpp CONTENT "${fonts_source}")
# The host UI's text (E5): the SDF font atlas on stb_truetype (third_party/stb, unmodified, built without warnings).
add_library(gt4_ui_text STATIC platform/ui/host/sdf_font.cpp platform/ui/host/stb_truetype_impl.cpp ${GT4_SHADER_OUT}/ui_fonts.cpp
    ${GT4_FONT_HEADERS})
target_include_directories(gt4_ui_text PUBLIC platform/ui platform/ui/host PRIVATE third_party/stb ${GT4_SHADER_OUT})
if(MSVC)
  target_compile_options(gt4_ui_text PRIVATE /W4)
  set_source_files_properties(platform/ui/host/stb_truetype_impl.cpp ${GT4_SHADER_OUT}/ui_fonts.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
  target_compile_options(gt4_ui_text PRIVATE -Wall -Wextra -Wpedantic)
  set_source_files_properties(platform/ui/host/stb_truetype_impl.cpp ${GT4_SHADER_OUT}/ui_fonts.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# AMD FidelityFX Super Resolution 2.2.1 runtime and its Vulkan backend (third_party/fidelityfx-fsr2, MIT).
# The precompiled shader tables of the SDK are replaced by platform/render/upscale/fsr2_shaders.cpp (our SPIR-V,
# reflected at run time). Vendored code is built without our warning flags; it is not modified.
add_library(gt4_fsr2 STATIC
    ${GT4_FSR2_DIR}/ffx_fsr2.cpp
    ${GT4_FSR2_DIR}/ffx_assert.cpp
    ${GT4_FSR2_DIR}/vk/ffx_fsr2_vk.cpp
    platform/render/upscale/fsr2_shaders.cpp)
target_include_directories(gt4_fsr2 PRIVATE ${GT4_FSR2_DIR} ${GT4_SHADER_OUT} platform/render platform/render/upscale)
# FFX_GCC only makes FFX_API empty (no dllexport): FSR 2 is linked statically.
target_compile_definitions(gt4_fsr2 PRIVATE VK_NO_PROTOTYPES FFX_GCC _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    _CRT_SECURE_NO_WARNINGS)
target_link_libraries(gt4_fsr2 PRIVATE Vulkan::Headers)
target_link_libraries(gt4_fsr2 PRIVATE ${GT4_VOLK_HEADERS})
if(MSVC)
  target_compile_options(gt4_fsr2 PRIVATE /W0 /FI${CMAKE_CURRENT_SOURCE_DIR}/platform/render/upscale/ffx_compat.h)
else()
  target_compile_options(gt4_fsr2 PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/platform/render/upscale/ffx_compat.h)
endif()

# AMD FidelityFX SDK 1.1.4's FSR 3.1 upscaler and its Vulkan backend (third_party/fidelityfx-sdk, MIT; PLAN.md B17), as
# FSR 2 above: the SDK's generated shader tables (ffx_shader_blobs.cpp, not vendored) are replaced by
# platform/render/upscale/fsr3_shaders.cpp, and the vendored code is built unmodified without our warning flags. Built
# on every platform, linked into nothing yet (B17 step 2 adds the backend that calls it).
add_library(gt4_fsr3 STATIC
    ${GT4_FSR3_DIR}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
    ${GT4_FSR3_DIR}/src/backends/vk/ffx_vk.cpp
    ${GT4_FSR3_DIR}/src/shared/ffx_assert.cpp
    ${GT4_FSR3_DIR}/src/shared/ffx_message.cpp
    ${GT4_FSR3_DIR}/src/shared/ffx_object_management.cpp
    ${GT4_FSR3_DIR}/src/shared/ffx_breadcrumbs_list.cpp
    platform/render/upscale/fsr3_shaders.cpp
    platform/render/upscale/fsr3_no_framegen.cpp
    ${GT4_SHADER_OUT}/gt4_fsr3_shaders.cpp
    ${GT4_FSR3_SHADER_HEADERS})
target_include_directories(gt4_fsr3 PUBLIC ${GT4_FSR3_DIR}/include)
target_include_directories(gt4_fsr3 PRIVATE ${GT4_FSR3_DIR}/src/shared
    ${GT4_FSR3_DIR}/src/backends/shared ${GT4_FSR3_DIR}/src/components/fsr3upscaler ${GT4_SHADER_OUT} platform/render
    platform/render/upscale)
target_compile_definitions(gt4_fsr3 PUBLIC VK_NO_PROTOTYPES)
target_compile_definitions(gt4_fsr3 PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS)
target_link_libraries(gt4_fsr3 PUBLIC Vulkan::Headers ${GT4_VOLK_HEADERS})
# The forced include is PUBLIC: it sets the SDK contexts' size, which is ABI (a caller allocates FfxFsr3UpscalerContext
# that gt4_fsr3 fills), so every C++ file of a target linking gt4_fsr3 gets the same one. gt4Fsr3ContextSize(),
# declared there, lets a caller check it at run time too. The warning switch-off stays private to the vendored code.
set(GT4_FSR3_COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/platform/render/upscale/ffx3_compat.h)
if(MSVC)
  target_compile_options(gt4_fsr3 PRIVATE /W0 PUBLIC $<$<COMPILE_LANGUAGE:CXX>:/FI${GT4_FSR3_COMPAT}>)
else()
  target_compile_options(gt4_fsr3 PRIVATE -w PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${GT4_FSR3_COMPAT}>")
endif()

# The FSR 3.1 backend of upscale::Upscaler (platform/render/upscale/fsr3.h; PLAN.md B17 step 2): the one library that
# links gt4_fsr3, PRIVATE, so the SDK's forced header and types stay in its own files and gt4_host sees only the
# interface (lanes B and D). Our code, with our warnings; the SDK's headers are system headers here.
add_library(gt4_fsr3_backend STATIC platform/render/upscale/fsr3.cpp)
target_include_directories(gt4_fsr3_backend PRIVATE platform/render platform/render/upscale)
target_include_directories(gt4_fsr3_backend SYSTEM PRIVATE ${GT4_FSR3_DIR}/include)
target_link_libraries(gt4_fsr3_backend PRIVATE gt4_fsr3 Vulkan::Headers ${GT4_VOLK_HEADERS})
if(MSVC)
  target_compile_options(gt4_fsr3_backend PRIVATE /W4)
else()
  target_compile_options(gt4_fsr3_backend PRIVATE -Wall -Wextra)
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
    platform/gsnative/texture_name.cpp platform/gsnative/texture_pack.cpp platform/gsnative/seam_anchor.cpp)
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
target_link_libraries(gt4_gs PUBLIC gt4_ps2gfx gt4_gsnative PRIVATE gt4_lighting gt4_audio)   # (F2b's glow classifier; D5)
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
# the light list, and the course lamps (the billboards' glow class, and the promotion of the glow billboards'
# candidates). No GPU; the host and the tests link it, so
# each is compiled once.
add_library(gt4_lighting STATIC platform/render/modern/light_sources.cpp platform/render/modern/lights_gather.cpp
    platform/render/modern/course_lamp_pass.cpp
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
# The host's SPU2 mixer (audio.mixer = host; the game's own mix otherwise), fed by gs_host's record dispatch.
add_library(gt4_audio STATIC platform/audio/host_mixer.cpp platform/audio/spatial/fft.cpp
                             platform/audio/spatial/hrtf.cpp)
target_include_directories(gt4_audio PUBLIC platform/audio PRIVATE include)
target_link_libraries(gt4_audio PUBLIC gt4_spu)
target_compile_definitions(gt4_audio PRIVATE $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)   # (getenv, fopen)
if(MSVC)
  target_compile_options(gt4_audio PRIVATE /W4)
  target_compile_options(gt4_spu PRIVATE /W4)
else()
  target_compile_options(gt4_audio PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(gt4_spu PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The game's pads from the keyboard and gamepads through an action map and the sticks' response (platform/input; no SDL
# calls, its headers).
add_library(gt4_input STATIC platform/input/input_map.cpp platform/input/response.cpp platform/input/devices.cpp
    platform/input/haptics.cpp platform/input/wheel_ffb.cpp
    platform/input/wheel_dfp.cpp platform/input/wheel_physics.cpp)
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
    platform/sdl/settings_audio.cpp
    platform/sdl/settings_ui.cpp)

add_executable(gt4_host
    platform/sdl/main.cpp
    platform/sdl/disc_setup.cpp
    platform/sdl/renderer.cpp
    platform/sdl/recorder.cpp
    platform/sdl/session.cpp
    platform/sdl/pad_devices.cpp
    platform/sdl/xinput_direct.cpp
    platform/sdl/wheel_haptics.cpp
    platform/sdl/device_settings.cpp
    ${GT4_SETTINGS_SOURCES}
    platform/sdl/display.cpp
    platform/render/vk.cpp
    platform/render/pipeline_cache_file.cpp
    platform/render/graph/render_graph.cpp
    platform/render/pipeline.cpp
    platform/render/display_feature.cpp
    platform/render/test_scene.cpp
    platform/render/gsn_scene.cpp
    platform/render/modern/lights_cluster.cpp
    platform/render/modern/parity_view.cpp
    platform/render/overlay.cpp
    platform/ui/host/settings_schema.cpp
    platform/ui/ui_records.cpp
    platform/ui/draw_list.cpp
    platform/ui/menu_model.cpp
    platform/ui/menu_layout.cpp
    platform/ui/host/ui_renderer.cpp
    platform/ui/host/page_test.cpp
    platform/ui/host/host_hub.cpp
    platform/render/upscale/fsr2.cpp
    platform/render/smaa_textures.cpp
    platform/sdl/stb_image_write_impl.cpp
    ${GT4_SHADER_OUT}/gt4_shaders.cpp
    ${GT4_SHADER_HEADERS})
target_include_directories(gt4_host PRIVATE platform/sdl platform/render platform/render/graph platform/render/modern
    platform/render/upscale
    ${GT4_SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${GT4_FSR2_DIR})
target_compile_definitions(gt4_host PRIVATE VK_NO_PROTOTYPES FFX_GCC GT4_LIGHTS_CLUSTER_GPU=1)
target_link_libraries(gt4_host PRIVATE SDL3::SDL3 Vulkan::Headers gt4_guest_abi gt4_fsr2 gt4_fsr3_backend gt4_gs gt4_saves gt4_input gt4_lighting gt4_ui_text ${GT4_VOLK} ${GT4_VOLK_HEADERS})
if(MSVC)
  target_compile_options(gt4_host PRIVATE /W4)
  # Generated SPIR-V tables and the volk loader are third-party style code: no warnings there.
  set_source_files_properties(${GT4_SHADER_OUT}/gt4_shaders.cpp
      platform/render/smaa_textures.cpp platform/sdl/stb_image_write_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
  target_compile_options(gt4_host PRIVATE -Wall -Wextra -Wpedantic)
  set_source_files_properties(${GT4_SHADER_OUT}/gt4_shaders.cpp
      platform/render/smaa_textures.cpp platform/sdl/stb_image_write_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
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

  add_executable(gt4_host_settings_test tests/host_settings.cpp ${GT4_SETTINGS_SOURCES})
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

  # The settings pages' renderer (M3): a draw list into output pixels on a recording sink, and M2's hub drawn whole.
  add_executable(gt4_ui_renderer_test tests/ui_renderer.cpp platform/ui/host/ui_renderer.cpp
                 platform/ui/menu_model.cpp platform/ui/menu_layout.cpp platform/ui/draw_list.cpp)
  target_include_directories(gt4_ui_renderer_test PRIVATE platform/ui platform/ui/host)
  if(MSVC)
    target_compile_options(gt4_ui_renderer_test PRIVATE /W4)
  else()
    target_compile_options(gt4_ui_renderer_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ui_renderer COMMAND gt4_ui_renderer_test)

  # The settings bridge (M4): every option on a page or hidden, sets through setOption, apply classes, the save rule
  # on a real settings file.
  add_executable(gt4_ui_settings_schema_test tests/ui_settings_schema.cpp platform/ui/host/settings_schema.cpp
                 platform/ui/ui_records.cpp platform/ui/draw_list.cpp ${GT4_SETTINGS_SOURCES})
  target_include_directories(gt4_ui_settings_schema_test PRIVATE platform/ui platform/ui/host platform/sdl platform/render platform/gs
                             include tests)
  if(MSVC)
    target_compile_options(gt4_ui_settings_schema_test PRIVATE /W4)
  else()
    target_compile_options(gt4_ui_settings_schema_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ui_settings_schema COMMAND gt4_ui_settings_schema_test)

  # The host-run hub (M9e): the page engine with no game, through the M4 bridge; its Quit row.
  add_executable(gt4_ui_host_hub_test tests/ui_host_hub.cpp platform/ui/host/host_hub.cpp
                 platform/ui/host/settings_schema.cpp platform/ui/ui_records.cpp platform/ui/draw_list.cpp
                 platform/ui/menu_model.cpp platform/ui/menu_layout.cpp ${GT4_SETTINGS_SOURCES})
  target_include_directories(gt4_ui_host_hub_test PRIVATE platform/ui platform/ui/host platform/sdl platform/render
                             platform/gs include tests)
  if(MSVC)
    target_compile_options(gt4_ui_host_hub_test PRIVATE /W4)
  else()
    target_compile_options(gt4_ui_host_hub_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ui_host_hub COMMAND gt4_ui_host_hub_test)

  # The host UI's SDF text (E5): the fonts load, layout and measure agree, UTF-8, a missing codepoint is a box.
  add_executable(gt4_ui_sdf_font_test tests/ui_sdf_font.cpp)
  target_link_libraries(gt4_ui_sdf_font_test PRIVATE gt4_ui_text)
  if(MSVC)
    target_compile_options(gt4_ui_sdf_font_test PRIVATE /W4)
  else()
    target_compile_options(gt4_ui_sdf_font_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ui_sdf_font COMMAND gt4_ui_sdf_font_test)

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
add_executable(gt4_parity_view_test tests/parity_view.cpp platform/render/modern/parity_view.cpp)
target_include_directories(gt4_parity_view_test PRIVATE platform/render/modern)
target_link_libraries(gt4_parity_view_test PRIVATE gt4_gsnative gt4_mesh_cache)
add_test(NAME parity_view COMMAND gt4_parity_view_test)
add_executable(gt4_seam_anchor_test tests/seam_anchor.cpp)
target_link_libraries(gt4_seam_anchor_test PRIVATE gt4_gsnative)
add_test(NAME seam_anchor COMMAND gt4_seam_anchor_test)
add_executable(gt4_gs_sprite_runs_test tests/gs_sprite_runs.cpp)
target_link_libraries(gt4_gs_sprite_runs_test PRIVATE gt4_gsnative)
add_test(NAME gs_sprite_runs COMMAND gt4_gs_sprite_runs_test)
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
  # advance()'s bulk steps against mix()'s per-sample voices: every Voice field and ENDX at every sample (PLAN.md D5).
  add_executable(gt4_spu2_advance_test tests/spu2_advance.cpp)
  target_link_libraries(gt4_spu2_advance_test PRIVATE gt4_spu)
  if(MSVC)
    target_compile_options(gt4_spu2_advance_test PRIVATE /W4)
  else()
    target_compile_options(gt4_spu2_advance_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME spu2_advance COMMAND gt4_spu2_advance_test)
  # The host's SPU2 mixer fed a game-side SPU2's records: its output is the game's own mix, sample for sample.
  add_executable(gt4_host_mixer_test tests/host_mixer.cpp)
  target_link_libraries(gt4_host_mixer_test PRIVATE gt4_audio)
  target_include_directories(gt4_host_mixer_test PRIVATE include)
  if(MSVC)
    target_compile_options(gt4_host_mixer_test PRIVATE /W4)
  else()
    target_compile_options(gt4_host_mixer_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME host_mixer COMMAND gt4_host_mixer_test)
  # --record's frame pacing (platform/sdl/recorder.h): a fixed-rate video from frames presented early, on time and late.
  add_executable(gt4_recorder_pacer_test tests/recorder_pacer.cpp)
  target_include_directories(gt4_recorder_pacer_test PRIVATE platform/sdl)
  if(MSVC)
    target_compile_options(gt4_recorder_pacer_test PRIVATE /W4)
  else()
    target_compile_options(gt4_recorder_pacer_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME recorder_pacer COMMAND gt4_recorder_pacer_test)
  # D9's HRTF renderer (platform/audio/spatial): its FFT against a DFT, its overlap-add against direct convolution
  add_executable(gt4_hrtf_test tests/hrtf.cpp)
  target_link_libraries(gt4_hrtf_test PRIVATE gt4_audio)
  if(MSVC)
    target_compile_options(gt4_hrtf_test PRIVATE /W4)
  else()
    target_compile_options(gt4_hrtf_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME hrtf COMMAND gt4_hrtf_test)

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
  # A wheel's LGDEV force effects (PLAN.md D7): GT4's effect blocks to the forces played, strength and ceiling applied.
  add_executable(gt4_input_wheel_ffb_test tests/input_wheel_ffb.cpp)
  target_link_libraries(gt4_input_wheel_ffb_test PRIVATE gt4_input)
  add_test(NAME input_wheel_ffb COMMAND gt4_input_wheel_ffb_test)
  # A wheel's input as GT4's Driving Force Pro (PLAN.md D7): the buttons through GT4's own remap, the pedals, the hat.
  add_executable(gt4_input_wheel_dfp_test tests/input_wheel_dfp.cpp)
  target_link_libraries(gt4_input_wheel_dfp_test PRIVATE gt4_input)
  add_test(NAME input_wheel_dfp COMMAND gt4_input_wheel_dfp_test)
  # A wheel's force feedback from the physics (PLAN.md D8): the torque's shape, kerbs, slip, the summed ceiling.
  add_executable(gt4_input_wheel_physics_test tests/input_wheel_physics.cpp)
  target_link_libraries(gt4_input_wheel_physics_test PRIVATE gt4_input)
  add_test(NAME input_wheel_physics COMMAND gt4_input_wheel_physics_test)

  # FSR 3.1's library links, and our shader provider serves every pass the SDK asks for, in both FFX_HALF variants,
  # with its bindings named as the SDK binds them; the jitter sequence against FSR 2's formula (PLAN.md B17; no GPU).
  # (FSR 2's provider reads the host's shader table, so the test compiles that table too)
  add_executable(gt4_fsr3_provider_test tests/fsr3_provider.cpp ${GT4_SHADER_OUT}/gt4_shaders.cpp
      ${GT4_SHADER_HEADERS})
  target_include_directories(gt4_fsr3_provider_test PRIVATE ${GT4_FSR3_DIR}/src/components/fsr3upscaler
      ${GT4_FSR3_DIR}/src/backends/shared ${GT4_FSR3_DIR}/src/shared ${GT4_SHADER_OUT} platform/render)
  target_link_libraries(gt4_fsr3_provider_test PRIVATE gt4_fsr3 gt4_fsr2 ${GT4_VOLK} ${GT4_VOLK_HEADERS})
  if(UNIX AND NOT APPLE)
    target_link_libraries(gt4_fsr3_provider_test PRIVATE ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME fsr3_provider COMMAND gt4_fsr3_provider_test)

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
  # The lighting's CPU parts (gt4_lighting), each test CPU only: the light gather (F2), and F2b's glow class and lamp
  # pass on a synthetic scene frame.
  foreach(test IN ITEMS lights_gather course_lamp_pass)
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
    # The night bands' triage score (tools/band_measure.py, C6b): a smooth synthetic frame scores 0, five bands are
    # found where they are; its PPM and PNG readers (all five PNG filters); numpy and plain Python agree.
    add_test(NAME band_measure_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/band_measure.py --self-test)
    # ORIG on another device class (tools/orig_compare.py, PLAN section 2): identical and 1-2 level noise pass, 32-GS-
    # pixel strips within 2 levels and the owner's 2-5 level bands fail, the 1-2 share against a baseline; numpy and
    # plain Python agree.
    add_test(NAME orig_compare_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/orig_compare.py --self-test)
    # The machine's slots (tools/slot.py): a live holder kept, a dead or reused one removed, on a temporary folder.
    add_test(NAME slot_selftest COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/slot.py --self-test)
    # The sources the game shares with the host (platform/ui, platform/audio/spu): none includes a host-only header,
    # so a release game build cannot fail at game_link on one (E5's 7755399 did; tools/game_link.py --check-shared).
    add_test(NAME game_link_shared
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/game_link.py --check-shared)
    # PLAN rule 9's comment-only proof (tools/comment_only.py): comment edits pass; a // comment ending in a
    # backslash, a /* */ pair around unchanged code, code, known.txt entries and added sources fail. Skipped (77)
    # without g++.
    add_test(NAME comment_only_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/comment_only.py --self-test)
    set_tests_properties(comment_only_selftest PROPERTIES SKIP_RETURN_CODE 77)
    # The disk pruner (tools/prune.py) on a temporary home: an idle clean worktree removed through git, dirty and
    # recent ones kept, newest packages and build trees kept, a junction removed without its target, saves kept.
    add_test(NAME prune_selftest COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/prune.py --self-test)
    # A replay comes out the same every run with frame interpolation and the HD pack on (C1): a capture with 3D
    # replayed twice, every frame bit for bit. Needs GT4_DETERMINISM_CAPTURE (captures are not in the repository)
    # and a Vulkan GPU; skipped (77) without the capture.
    add_test(NAME replay_determinism
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_determinism.py $<TARGET_FILE:gt4_host>)
    set_tests_properties(replay_determinism PROPERTIES SKIP_RETURN_CODE 77)
    # The settings hub drawn by the host (MENU_DESIGN M3) at 720p, 1440p and 4K against tests/goldens/ui (a per-pixel
    # tolerance and a cap on the pixels over it), and its white in HDR at display.hdr_paper_white = 200 as the PQ
    # code. Needs a Vulkan GPU; skipped (77) without one.
    add_test(NAME ui_page_goldens
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_page_goldens.py $<TARGET_FILE:gt4_host>)
    set_tests_properties(ui_page_goldens PROPERTIES SKIP_RETURN_CODE 77)
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

