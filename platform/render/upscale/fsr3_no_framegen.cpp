// The one frame-generation symbol the vendored FidelityFX SDK's Vulkan backend needs (third_party/fidelityfx-sdk;
// PLAN.md B17). ffx_vk.cpp's ffxGetInterfaceVK stores the address of ffxSetFrameGenerationConfigToSwapchainVK in the
// backend interface; its definition lives in the SDK's frame-interpolation swapchain, which is not vendored (B17: the
// upscaler only). Without a definition nothing that links gt4_fsr3 links. This one refuses: FSR 3.1's upscaler never
// calls it, and anything that did would get an error, not frame generation (third_party/README.md says so).
#include <FidelityFX/host/backends/vk/ffx_vk.h>

#include <cstdio>

extern "C" FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) {
    static bool said = false;
    if (!said) {
        said = true;
        std::fprintf(stderr, "FSR 3: frame generation asked for; it isn't part of this build (refused)\n");
    }
    return FFX_ERROR_INVALID_ARGUMENT;
}
