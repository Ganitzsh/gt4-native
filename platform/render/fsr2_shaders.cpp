// Shader blobs for the vendored FSR 2 Vulkan backend (third_party/fidelityfx-fsr2/vk/ffx_fsr2_vk.cpp).
// The SDK ships them as precompiled permutation tables made by its Windows-only shader compiler; here
// each pass is compiled by glslang during the build (cmake/Host.cmake) with the options this host always
// uses, and the resource names and bindings the backend needs are read back from the SPIR-V.
#include "gt4_shaders.h"

#include "vk/shaders/ffx_fsr2_shaders_vk.h"

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Reflection {
    std::vector<std::string> storageNames, sampledNames, uniformNames;
    std::vector<uint32_t> storageBindings, sampledBindings, uniformBindings;
    std::vector<const char*> storagePointers, sampledPointers, uniformPointers;
};

// Minimal SPIR-V reflection: descriptor variables of set 1 (set 0 holds the backend's samplers) that the
// code really uses. The FSR 2 sources declare some resources a pass never touches (the depth clip pass
// declares prepared_input_color as both input and output); like the SDK's own reflection, those are left
// out, or the backend would bind one texture through two descriptors in different layouts.
Reflection reflect(gt4::vk::ShaderCode code) {
    const uint32_t* words = code.words;
    const size_t count = code.bytes / 4;
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, uint32_t> bindings, sets;
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
        case 25: types[w[1]] = {25, w[7], 0}; break;                                  // OpTypeImage: Sampled
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
        if (!sets.count(variable.id) || sets[variable.id] != 1 || !bindings.count(variable.id)) continue;
        const auto pointer = types[variable.type];
        const auto pointee = types[pointer.b];
        const uint32_t binding = bindings[variable.id];
        const std::string name = names[variable.id];
        if (pointee.op == 25 && pointee.a == 2) { result.storageNames.push_back(name); result.storageBindings.push_back(binding); }
        else if (pointee.op == 25 && pointee.a == 1) { result.sampledNames.push_back(name); result.sampledBindings.push_back(binding); }
        else if (pointee.op == 30 && variable.storage == 2) { result.uniformNames.push_back(name); result.uniformBindings.push_back(binding); }
    }
    for (const auto& name : result.storageNames) result.storagePointers.push_back(name.c_str());
    for (const auto& name : result.sampledNames) result.sampledPointers.push_back(name.c_str());
    for (const auto& name : result.uniformNames) result.uniformPointers.push_back(name.c_str());
    return result;
}

gt4::vk::ShaderCode passCode(FfxFsr2Pass pass, bool sharpen) {
    using namespace gt4::shaders;
    switch (pass) {
    case FFX_FSR2_PASS_DEPTH_CLIP: return fsr2_depth_clip();
    case FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH: return fsr2_reconstruct_previous_depth();
    case FFX_FSR2_PASS_LOCK: return fsr2_lock();
    case FFX_FSR2_PASS_ACCUMULATE:
    case FFX_FSR2_PASS_ACCUMULATE_SHARPEN: return sharpen ? fsr2_accumulate_sharpen() : fsr2_accumulate();
    case FFX_FSR2_PASS_RCAS: return fsr2_rcas();
    case FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID: return fsr2_compute_luminance_pyramid();
    case FFX_FSR2_PASS_GENERATE_REACTIVE: return fsr2_autogen_reactive();
    case FFX_FSR2_PASS_TCR_AUTOGENERATE: return fsr2_tcr_autogen();
    default: return {};
    }
}

}  // namespace

extern "C" Fsr2ShaderBlobVK fsr2GetPermutationBlobByIndexVK(FfxFsr2Pass passId, uint32_t permutationOptions) {
    // Only the permutation this host compiles exists: HDR colour, render-resolution and unjittered motion
    // vectors, inverted depth. Wave64, FP16 and the Lanczos LUT choice are performance options of the same
    // shader and fall back to it.
    const uint32_t required = FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT | FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS |
                              FSR2_SHADER_PERMUTATION_DEPTH_INVERTED;
    const uint32_t forbidden = FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS;
    Fsr2ShaderBlobVK empty{};
    if ((permutationOptions & required) != required || (permutationOptions & forbidden)) return empty;
    const bool sharpen = (permutationOptions & FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING) != 0;
    static std::map<uint32_t, Reflection> cache;
    const uint32_t key = static_cast<uint32_t>(passId) * 2u + (sharpen ? 1u : 0u);
    const auto code = passCode(passId, sharpen);
    if (!code.words) return empty;
    auto found = cache.find(key);
    if (found == cache.end()) found = cache.emplace(key, reflect(code)).first;
    const Reflection& r = found->second;
    return Fsr2ShaderBlobVK{
        reinterpret_cast<const uint8_t*>(code.words), static_cast<uint32_t>(code.bytes),
        static_cast<uint32_t>(r.storageNames.size()), static_cast<uint32_t>(r.sampledNames.size()),
        static_cast<uint32_t>(r.uniformNames.size()),
        const_cast<const char**>(r.storagePointers.data()), r.storageBindings.data(),
        const_cast<const char**>(r.sampledPointers.data()), r.sampledBindings.data(),
        const_cast<const char**>(r.uniformPointers.data()), r.uniformBindings.data()};
}
