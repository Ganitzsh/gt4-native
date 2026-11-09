// FSR 3.1's library links, and our shader provider serves what the SDK asks for (PLAN.md B17; no GPU needed).
// - It links: the SDK's context creation and Vulkan interface are referenced, so gt4_fsr3's whole backend object is
//   pulled in, with every symbol it needs (the gap ffx_vk.cpp left: platform/render/upscale/fsr3_no_framegen.cpp),
//   beside FSR 2.2.1's Vulkan backend, as in the host: no name defined by both (ffx3_compat.h's renames).
// - The context's size is the one gt4_fsr3 was built with (the forced header's size, ffx3_compat.h).
// - Every pass, with and without sharpening, in both FFX_HALF variants, has a SPIR-V blob from
//   ffxGetPermutationBlobByIndex, and the bindings reflected from it are named by kind as the SDK binds them (sampled
//   r_*, storage rw_*, constant buffers cb*). Permutations this host never gives are refused. The 16-bit code only
//   where the device enabled 16-bit arithmetic (gt4Fsr3AllowHalf). rw_luma_history is declared Rgba16f, the format
//   the SDK makes the luma history in (its GLSL says rgba8: cmake/Host.cmake corrects a copy of the header).
// - The jitter sequence: FSR 3.1's phase count against FSR 2's formula, 8 x (display / render)^2 (not FSR 2's code,
//   so a bug the two shared couldn't pass both), and offsets within half a pixel.
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <ffx_shader_blobs.h>

#include "ffx_fsr3upscaler_private.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// FSR 2.2.1's Vulkan backend (gt4_fsr2), declared here rather than through its headers, which clash with the SDK's in
// one file: the host links both libraries, so this test does too, and a name both define fails its link (ffx3_compat.h
// renames the twelve found so far).
extern "C" size_t ffxFsr2GetScratchMemorySizeVK(VkPhysicalDevice physicalDevice);

namespace {

int failures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++failures;
    }
}

constexpr uint32_t kRequired = FSR3UPSCALER_SHADER_PERMUTATION_HDR_COLOR_INPUT |
                               FSR3UPSCALER_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS |
                               FSR3UPSCALER_SHADER_PERMUTATION_DEPTH_INVERTED;

// The provider fills a blob whose members are const: it is read from raw storage.
struct Blob {
    alignas(FfxShaderBlob) unsigned char storage[sizeof(FfxShaderBlob)];
    const FfxShaderBlob& get() const { return *reinterpret_cast<const FfxShaderBlob*>(storage); }
};

FfxErrorCode ask(uint32_t pass, uint32_t options, Blob& blob) {
    std::memset(blob.storage, 0, sizeof blob.storage);
    return ffxGetPermutationBlobByIndex(FFX_EFFECT_FSR3UPSCALER, static_cast<FfxPass>(pass),
                                        FFX_BIND_COMPUTE_SHADER_STAGE, options,
                                        reinterpret_cast<FfxShaderBlob*>(blob.storage));
}

// The SPIR-V image format (OpTypeImage's Image Format operand) of the storage image named `variable`, or -1 when the
// code has none by that name.
int imageFormat(const uint8_t* data, uint32_t size, const char* variable) {
    std::vector<uint32_t> w(size / 4);
    std::memcpy(w.data(), data, w.size() * 4);
    uint32_t id = 0;
    std::vector<uint32_t> pointerTo(w.size() ? 1u << 20 : 0), formatOf(pointerTo.size()), typeOf(pointerTo.size());
    for (size_t i = 5; i < w.size();) {
        const uint32_t length = w[i] >> 16, op = w[i] & 0xffffu;
        if (length == 0 || i + length > w.size()) break;
        if (op == 5 && std::strcmp(reinterpret_cast<const char*>(&w[i + 2]), variable) == 0) id = w[i + 1];   // OpName
        if (op == 25 && length > 8 && w[i + 1] < formatOf.size()) formatOf[w[i + 1]] = w[i + 8] + 1;   // OpTypeImage
        if (op == 32 && w[i + 1] < pointerTo.size()) pointerTo[w[i + 1]] = w[i + 3];                   // OpTypePointer
        if (op == 59 && w[i + 2] < typeOf.size()) typeOf[w[i + 2]] = w[i + 1];                         // OpVariable
        i += length;
    }
    if (!id || id >= typeOf.size()) return -1;
    const uint32_t image = pointerTo[typeOf[id]];
    return image < formatOf.size() && formatOf[image] ? static_cast<int>(formatOf[image]) - 1 : -1;
}

void checkNames(const char* const* names, uint32_t count, const char* prefix, const std::string& where) {
    std::set<std::string> seen;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string name = names && names[i] ? names[i] : "";
        expect(name.rfind(prefix, 0) == 0, where + ": binding '" + name + "' not named " + prefix + "*");
        expect(seen.insert(name).second, where + ": binding '" + name + "' twice");
    }
}

}  // namespace

int main() {
    // It links, with the backend's object pulled in (these are never called: no device here).
    volatile const void* linked[] = {reinterpret_cast<const void*>(&ffxFsr3UpscalerContextCreate),
                                     reinterpret_cast<const void*>(&ffxGetInterfaceVK),
                                     reinterpret_cast<const void*>(&ffxGetDeviceVK),
                                     reinterpret_cast<const void*>(&ffxSetFrameGenerationConfigToSwapchainVK),
                                     reinterpret_cast<const void*>(&ffxFsr2GetScratchMemorySizeVK)};
    for (const volatile void* p : linked) expect(p != nullptr, "an SDK entry point is null");

    expect(gt4Fsr3ContextSize() == sizeof(FfxFsr3UpscalerContext),
           "gt4_fsr3's FfxFsr3UpscalerContext is " + std::to_string(gt4Fsr3ContextSize()) + " bytes, this file's " +
               std::to_string(sizeof(FfxFsr3UpscalerContext)));

    const struct { uint32_t pass; const char* name; bool sharpen; } passes[] = {
        {FFX_FSR3UPSCALER_PASS_PREPARE_INPUTS, "prepare inputs", false},
        {FFX_FSR3UPSCALER_PASS_LUMA_PYRAMID, "luma pyramid", false},
        {FFX_FSR3UPSCALER_PASS_SHADING_CHANGE_PYRAMID, "shading change pyramid", false},
        {FFX_FSR3UPSCALER_PASS_SHADING_CHANGE, "shading change", false},
        {FFX_FSR3UPSCALER_PASS_PREPARE_REACTIVITY, "prepare reactivity", false},
        {FFX_FSR3UPSCALER_PASS_LUMA_INSTABILITY, "luma instability", false},
        {FFX_FSR3UPSCALER_PASS_ACCUMULATE, "accumulate", false},
        {FFX_FSR3UPSCALER_PASS_ACCUMULATE_SHARPEN, "accumulate (sharpen)", true},
        {FFX_FSR3UPSCALER_PASS_RCAS, "RCAS", false},
        {FFX_FSR3UPSCALER_PASS_DEBUG_VIEW, "debug view", false},
        {FFX_FSR3UPSCALER_PASS_GENERATE_REACTIVE, "generate reactive", false},
    };
    // A device that hasn't enabled 16-bit arithmetic gets the 32-bit code even when the SDK asks for FP16
    {
        gt4Fsr3AllowHalf(false);
        Blob off, plain;
        const uint32_t halfBit = FSR3UPSCALER_SHADER_PERMUTATION_ALLOW_FP16;
        expect(ask(FFX_FSR3UPSCALER_PASS_ACCUMULATE, kRequired | halfBit, off) == FFX_OK &&
                   ask(FFX_FSR3UPSCALER_PASS_ACCUMULATE, kRequired, plain) == FFX_OK &&
                   off.get().data == plain.get().data,
               "16-bit served to a device that hasn't enabled it");
    }
    gt4Fsr3AllowHalf(true);   // (the rest as on a device with it enabled)
    int lumaHistories = 0;
    for (const auto& p : passes) {
        const uint8_t* variant[2] = {nullptr, nullptr};
        for (int half = 0; half < 2; ++half) {
            const uint32_t sharpenBit = FSR3UPSCALER_SHADER_PERMUTATION_ENABLE_SHARPENING;
            const uint32_t halfBit = FSR3UPSCALER_SHADER_PERMUTATION_ALLOW_FP16;
            const uint32_t options = kRequired | (p.sharpen ? sharpenBit : 0u) | (half ? halfBit : 0u);
            const std::string where = std::string(p.name) + (half ? " (16-bit allowed)" : " (32-bit)");
            Blob blob;
            if (ask(p.pass, options, blob) != FFX_OK) {
                expect(false, where + ": no blob");
                continue;
            }
            const FfxShaderBlob& b = blob.get();
            variant[half] = b.data;
            uint32_t magic = 0;
            if (b.data && b.size >= 4) std::memcpy(&magic, b.data, 4);
            expect(b.data && b.size >= 20 && b.size % 4 == 0 && magic == 0x07230203u, where + ": not SPIR-V");
            expect(b.cbvCount >= 1, where + ": no constant buffer");
            expect(b.srvTextureCount + b.uavTextureCount >= 1, where + ": no textures");
            expect(b.srvBufferCount == 0 && b.uavBufferCount == 0 && b.samplerCount == 0 && b.rtAccelStructCount == 0,
                   where + ": a binding kind FSR 3's upscaler doesn't use");
            checkNames(b.boundConstantBufferNames, b.cbvCount, "cb", where);
            checkNames(b.boundSRVTextureNames, b.srvTextureCount, "r_", where);
            checkNames(b.boundUAVTextureNames, b.uavTextureCount, "rw_", where);
            // the luma history as the SDK makes it (SPIR-V ImageFormat Rgba16f = 2; the SDK's GLSL said Rgba8 = 4)
            const int luma = imageFormat(b.data, b.size, "rw_luma_history");
            expect(luma == -1 || luma == 2, where + ": rw_luma_history is format " + std::to_string(luma) +
                                                ", not Rgba16f (2)");
            if (luma == 2) ++lumaHistories;
        }
        // RCAS is 32-bit only (the component never asks it for 16-bit); every other pass has its own 16-bit code
        if (p.pass == FFX_FSR3UPSCALER_PASS_RCAS) expect(variant[0] == variant[1], "RCAS: a 16-bit variant served");
        else expect(variant[0] && variant[1] && variant[0] != variant[1], std::string(p.name) + ": one code for both");
    }

    expect(lumaHistories > 0, "no pass declares rw_luma_history: the check above checked nothing");

    Blob blob;
    expect(ask(FFX_FSR3UPSCALER_PASS_ACCUMULATE, kRequired & ~FSR3UPSCALER_SHADER_PERMUTATION_HDR_COLOR_INPUT, blob) !=
               FFX_OK, "an LDR permutation served");
    expect(ask(FFX_FSR3UPSCALER_PASS_ACCUMULATE, kRequired & ~FSR3UPSCALER_SHADER_PERMUTATION_DEPTH_INVERTED, blob) !=
               FFX_OK, "a non-inverted depth permutation served");
    expect(ask(FFX_FSR3UPSCALER_PASS_ACCUMULATE, kRequired & ~FSR3UPSCALER_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS,
               blob) != FFX_OK, "display-resolution motion vectors served");
    expect(ask(FFX_FSR3UPSCALER_PASS_ACCUMULATE, kRequired | FSR3UPSCALER_SHADER_PERMUTATION_JITTER_MOTION_VECTORS,
               blob) != FFX_OK, "jittered motion vectors served");
    expect(ffxGetPermutationBlobByIndex(FFX_EFFECT_FSR2, FFX_FSR3UPSCALER_PASS_ACCUMULATE,
                                        FFX_BIND_COMPUTE_SHADER_STAGE, kRequired,
                                        reinterpret_cast<FfxShaderBlob*>(blob.storage)) != FFX_OK,
           "another effect served");

    const struct { int32_t render, display; } sizes[] = {
        {1920, 1920}, {1280, 1920}, {1280, 2560}, {960, 2880}, {640, 1280}};
    for (const auto& s : sizes) {
        const int32_t expected =
            static_cast<int32_t>(8.0 * std::pow(static_cast<double>(s.display) / s.render, 2.0));
        const int32_t phases = ffxFsr3UpscalerGetJitterPhaseCount(s.render, s.display);
        expect(phases == expected, "jitter phases for " + std::to_string(s.render) + " to " +
                                       std::to_string(s.display) + ": " + std::to_string(phases) +
                                       ", the formula gives " + std::to_string(expected));
        for (int32_t i = 0; i < phases; ++i) {
            float x = 9.0f, y = 9.0f;
            ffxFsr3UpscalerGetJitterOffset(&x, &y, i, phases);
            expect(std::fabs(x) <= 0.5f && std::fabs(y) <= 0.5f, "a jitter offset past half a pixel");
        }
    }

    if (failures) std::printf("%d failure(s)\n", failures);
    else std::printf("fsr3_provider: all passed\n");
    return failures ? 1 : 0;
}
