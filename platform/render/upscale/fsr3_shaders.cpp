// Shader blobs for the vendored FidelityFX SDK's Vulkan backend (third_party/fidelityfx-sdk, FSR 3.1's upscaler only;
// PLAN.md B17). The SDK gets its blobs from tables its Windows-only shader compiler generates
// (src/backends/shared/ffx_shader_blobs.cpp, not vendored); here each pass is compiled by glslang during the build
// (cmake/Host.cmake) in the permutations this host uses, and the resources the backend binds are read back from the
// SPIR-V, as fsr2_shaders.cpp does for FSR 2.
//
// Permutations: the inputs are always HDR colour, render-resolution motion vectors without jitter, and inverted depth.
// The component also asks for the Lanczos LUT and Wave64 on some GPUs (32- to 64-lane waves); those are performance
// options of the same shader, served by the one reference shader, so every device reprojects alike. FP16 is honoured
// (the component sets it from the device's capabilities, never for RCAS): the 16-bit variant where the device has
// it, the 32-bit one otherwise, and which one each device got is logged once.
#include "gt4_fsr3_shaders.h"

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_interface.h>
#include <FidelityFX/host/ffx_types.h>
#include <ffx_shader_blobs.h>

#include "ffx_fsr3upscaler_private.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// The public context the caller allocates must hold the SDK's private one. The SDK asserts this inside
// ffx_fsr3upscaler.cpp; this one is ours, so a later SDK version can't drop it (with a 4-byte wchar_t the private
// context outgrows the SDK's default size: ffx3_compat.h).
static_assert(sizeof(FfxFsr3UpscalerContext) >= sizeof(FfxFsr3UpscalerContext_Private),
              "FfxFsr3UpscalerContext is smaller than the SDK's private context: raise FFX_SDK_DEFAULT_CONTEXT_SIZE");

std::size_t gt4Fsr3ContextSize() { return sizeof(FfxFsr3UpscalerContext); }

namespace {

struct Binding {
    std::string name;
    uint32_t binding = 0, count = 1;
};

struct Reflection {
    std::vector<Binding> sampled, storage, uniform;   // SRV textures, UAV textures, constant buffers
    // the backend's view of them: names, bindings, counts and spaces (all 0) per kind
    std::vector<const char*> sampledNames, storageNames, uniformNames;
    std::vector<uint32_t> sampledBindings, storageBindings, uniformBindings;
    std::vector<uint32_t> sampledCounts, storageCounts, uniformCounts;
    std::vector<uint32_t> sampledSpaces, storageSpaces, uniformSpaces;
};

// Minimal SPIR-V reflection: the descriptor variables of set 0 that the code really uses (the FSR 3 sources declare
// resources a pass never touches; like the SDK's own reflection, those are left out), with their array lengths.
// Samplers are skipped: the backend binds its static samplers itself, at bindings 1000 and up.
Reflection reflect(gt4::vk::ShaderCode code) {
    const uint32_t* words = code.words;
    const size_t count = code.bytes / 4;
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, uint32_t> bindings, sets, constants;
    struct Type { uint32_t op = 0, a = 0, b = 0; };
    std::map<uint32_t, Type> types;
    struct Variable { uint32_t id, type, storage; };
    std::vector<Variable> variables;
    std::set<uint32_t> used;
    for (size_t i = 5; i < count;) {
        const uint32_t length = words[i] >> 16, op = words[i] & 0xffffu;
        if (length == 0 || i + length > count) break;
        const uint32_t* w = words + i;
        switch (op) {
        case 5: names[w[1]] = reinterpret_cast<const char*>(w + 2); break;          // OpName
        case 71:                                                                      // OpDecorate
            if (w[2] == 33) bindings[w[1]] = w[3];                                    // Binding
            if (w[2] == 34) sets[w[1]] = w[3];                                        // DescriptorSet
            break;
        case 43: if (length >= 4) constants[w[2]] = w[3]; break;                      // OpConstant (32-bit)
        case 25: types[w[1]] = {25, w[7], 0}; break;                                  // OpTypeImage: Sampled
        case 26: types[w[1]] = {26, 0, 0}; break;                                     // OpTypeSampler
        case 28: types[w[1]] = {28, w[2], w[3]}; break;                               // OpTypeArray: element, length
        case 30: types[w[1]] = {30, 0, 0}; break;                                     // OpTypeStruct
        case 32: types[w[1]] = {32, w[2], w[3]}; break;                               // OpTypePointer
        case 59: variables.push_back({w[2], w[1], w[3]}); break;                      // OpVariable
        // Instructions that reference a variable through a pointer operand.
        case 60: case 61: case 65: case 66: case 67: used.insert(w[3]); break;        // texel pointer, load, access chains
        case 62: used.insert(w[1]); break;                                            // OpStore
        case 63: used.insert(w[1]); used.insert(w[2]); break;                         // OpCopyMemory
        case 57: for (uint32_t a = 4; a < length; ++a) used.insert(w[a]); break;     // OpFunctionCall arguments
        default: break;
        }
        i += length;
    }
    Reflection result;
    for (const auto& variable : variables) {
        if (variable.storage != 0 && variable.storage != 2) continue;                 // UniformConstant, Uniform
        if (!used.count(variable.id)) continue;
        if (!sets.count(variable.id) || sets[variable.id] != 0 || !bindings.count(variable.id)) continue;
        Type pointee = types[types[variable.type].b];
        uint32_t elements = 1;
        if (pointee.op == 28) {                                                       // an array of descriptors
            elements = constants.count(pointee.b) ? constants[pointee.b] : 1;
            pointee = types[pointee.a];
        }
        Binding b{names[variable.id], bindings[variable.id], elements};
        if (pointee.op == 25 && pointee.a == 1) result.sampled.push_back(b);
        else if (pointee.op == 25 && pointee.a == 2) result.storage.push_back(b);
        else if (pointee.op == 30 && variable.storage == 2) result.uniform.push_back(b);
    }
    const auto fill = [](const std::vector<Binding>& from, std::vector<const char*>& n, std::vector<uint32_t>& b,
                         std::vector<uint32_t>& c, std::vector<uint32_t>& s) {
        for (const Binding& x : from) {
            n.push_back(x.name.c_str());
            b.push_back(x.binding);
            c.push_back(x.count);
            s.push_back(0);
        }
    };
    fill(result.sampled, result.sampledNames, result.sampledBindings, result.sampledCounts, result.sampledSpaces);
    fill(result.storage, result.storageNames, result.storageBindings, result.storageCounts, result.storageSpaces);
    fill(result.uniform, result.uniformNames, result.uniformBindings, result.uniformCounts, result.uniformSpaces);
    return result;
}

gt4::vk::ShaderCode passCode(uint32_t pass, bool sharpen, bool half) {
    using namespace gt4::shaders;
    switch (pass) {
    case FFX_FSR3UPSCALER_PASS_PREPARE_INPUTS: return half ? fsr3_prepare_inputs_h1() : fsr3_prepare_inputs_h0();
    case FFX_FSR3UPSCALER_PASS_LUMA_PYRAMID: return half ? fsr3_luma_pyramid_h1() : fsr3_luma_pyramid_h0();
    case FFX_FSR3UPSCALER_PASS_SHADING_CHANGE_PYRAMID:
        return half ? fsr3_shading_change_pyramid_h1() : fsr3_shading_change_pyramid_h0();
    case FFX_FSR3UPSCALER_PASS_SHADING_CHANGE: return half ? fsr3_shading_change_h1() : fsr3_shading_change_h0();
    case FFX_FSR3UPSCALER_PASS_PREPARE_REACTIVITY:
        return half ? fsr3_prepare_reactivity_h1() : fsr3_prepare_reactivity_h0();
    case FFX_FSR3UPSCALER_PASS_LUMA_INSTABILITY: return half ? fsr3_luma_instability_h1() : fsr3_luma_instability_h0();
    case FFX_FSR3UPSCALER_PASS_ACCUMULATE:
    case FFX_FSR3UPSCALER_PASS_ACCUMULATE_SHARPEN:
        if (sharpen) return half ? fsr3_accumulate_sharpen_h1() : fsr3_accumulate_sharpen_h0();
        return half ? fsr3_accumulate_h1() : fsr3_accumulate_h0();
    case FFX_FSR3UPSCALER_PASS_RCAS: return fsr3_rcas_h0();   // (the component never asks RCAS for 16-bit)
    case FFX_FSR3UPSCALER_PASS_DEBUG_VIEW: return half ? fsr3_debug_view_h1() : fsr3_debug_view_h0();
    case FFX_FSR3UPSCALER_PASS_GENERATE_REACTIVE: return half ? fsr3_autogen_reactive_h1() : fsr3_autogen_reactive_h0();
    default: return {};
    }
}

}  // namespace

extern "C" FfxErrorCode ffxGetPermutationBlobByIndex(FfxEffect effectId, FfxPass passId, FfxBindStage,
                                                    uint32_t permutationOptions, FfxShaderBlob* outBlob) {
    if (effectId != FFX_EFFECT_FSR3UPSCALER || !outBlob) return FFX_ERROR_INVALID_ARGUMENT;
    // Only the inputs this host gives exist: HDR colour, render-resolution and unjittered motion vectors, inverted
    // depth. Anything else is refused, so a mismatch fails context creation instead of drawing wrongly.
    const uint32_t required = FSR3UPSCALER_SHADER_PERMUTATION_HDR_COLOR_INPUT |
                              FSR3UPSCALER_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS |
                              FSR3UPSCALER_SHADER_PERMUTATION_DEPTH_INVERTED;
    const uint32_t forbidden = FSR3UPSCALER_SHADER_PERMUTATION_JITTER_MOTION_VECTORS;
    if ((permutationOptions & required) != required || (permutationOptions & forbidden)) return FFX_ERROR_INVALID_ARGUMENT;
    const bool sharpen = (permutationOptions & FSR3UPSCALER_SHADER_PERMUTATION_ENABLE_SHARPENING) != 0;
    const bool half = (permutationOptions & FSR3UPSCALER_SHADER_PERMUTATION_ALLOW_FP16) != 0 &&
                      passId != FFX_FSR3UPSCALER_PASS_RCAS;
    const gt4::vk::ShaderCode code = passCode(passId, sharpen, half);
    if (!code.words) return FFX_ERROR_INVALID_ARGUMENT;
    static std::mutex lock;
    static std::map<uint32_t, Reflection> cache;
    static bool said[2] = {false, false};
    std::lock_guard<std::mutex> guard(lock);
    if (passId != FFX_FSR3UPSCALER_PASS_RCAS && !said[half]) {   // (which variant this device runs: C6b's Odin report)
        said[half] = true;
        std::printf("FSR 3: %s passes (the device %s 16-bit shader arithmetic)\n", half ? "16-bit" : "32-bit",
                    half ? "has" : "lacks");
        std::fflush(stdout);
    }
    const uint32_t key = passId * 4u + (sharpen ? 2u : 0u) + (half ? 1u : 0u);
    auto found = cache.find(key);
    if (found == cache.end()) found = cache.emplace(key, reflect(code)).first;
    const Reflection& r = found->second;
    const auto data = [](const std::vector<const char*>& v) { return const_cast<const char**>(v.empty() ? nullptr : v.data()); };
    const auto ptr = [](const std::vector<uint32_t>& v) { return v.empty() ? nullptr : v.data(); };
    const FfxShaderBlob blob = {
        reinterpret_cast<const uint8_t*>(code.words), static_cast<uint32_t>(code.bytes),
        static_cast<uint32_t>(r.uniform.size()), static_cast<uint32_t>(r.sampled.size()),
        static_cast<uint32_t>(r.storage.size()), 0u, 0u, 0u, 0u,
        data(r.uniformNames), ptr(r.uniformBindings), ptr(r.uniformCounts), ptr(r.uniformSpaces),
        data(r.sampledNames), ptr(r.sampledBindings), ptr(r.sampledCounts), ptr(r.sampledSpaces),
        data(r.storageNames), ptr(r.storageBindings), ptr(r.storageCounts), ptr(r.storageSpaces),
        nullptr, nullptr, nullptr, nullptr,   // SRV buffers: none in FSR 3's upscaler
        nullptr, nullptr, nullptr, nullptr,   // UAV buffers: none
        nullptr, nullptr, nullptr, nullptr,   // samplers: the backend's static ones
        nullptr, nullptr, nullptr, nullptr};  // acceleration structures: none
    std::memcpy(static_cast<void*>(outBlob), &blob, sizeof blob);
    return FFX_OK;
}

// No Wave64 variant: GLSL has no wave-size control here, and the reference shader serves every device.
extern "C" FfxErrorCode ffxIsWave64(FfxEffect, uint32_t, bool& isWave64) {
    isWave64 = false;
    return FFX_OK;
}
