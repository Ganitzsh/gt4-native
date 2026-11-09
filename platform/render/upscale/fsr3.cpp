#include "fsr3.h"

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

namespace gt4 {

namespace {

// Our pipeline cache for the SDK's pipelines (vk::Device::pipelineCache). FidelityFX SDK 1.1.4 has no field for one:
// its backend loads every device function through the vkDeviceProcAddr we give it, and creates its compute
// pipelines with a null cache (ffx_vk.cpp). So vkCreateComputePipelines is handed out wrapped, and a null cache in it
// becomes ours. Ours is made with flags 0: internally synchronized, as the SDK's calls from any thread need. With no
// cache (GT4_PIPELINE_CACHE=0) nothing is wrapped and the SDK's null passes through. One device per host.
VkPipelineCache g_pipelineCache = VK_NULL_HANDLE;
PFN_vkCreateComputePipelines g_createComputePipelines = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL createComputePipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                      const VkComputePipelineCreateInfo* infos,
                                                      const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
    return g_createComputePipelines(device, cache != VK_NULL_HANDLE ? cache : g_pipelineCache, count, infos, allocator,
                                    pipelines);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProcAddr(VkDevice device, const char* name) {
    if (g_pipelineCache != VK_NULL_HANDLE && name && std::strcmp(name, "vkCreateComputePipelines") == 0) {
        g_createComputePipelines =
            reinterpret_cast<PFN_vkCreateComputePipelines>(vkGetDeviceProcAddr(device, "vkCreateComputePipelines"));
        if (g_createComputePipelines) return reinterpret_cast<PFN_vkVoidFunction>(&createComputePipelines);
    }
    return vkGetDeviceProcAddr(device, name);
}

void message(FfxMsgType type, const wchar_t* text) {
    std::fprintf(stderr, "FSR 3 %s: %ls\n", type == FFX_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
}

// The three images FSR 3.1 binds whatever the call (ffx_fsr3upscaler.cpp registers them unconditionally): dilated
// depth, dilated motion vectors, and the previous frame's reconstructed nearest depth, read the frame after.
struct Shared {
    const char* name;
    const wchar_t* wideName;
    VkFormat format;
    FfxSurfaceFormat expected;
};
constexpr Shared kShared[3] = {
    {"fsr3 dilated depth", L"FSR 3 dilated depth", VK_FORMAT_R32_SFLOAT, FFX_SURFACE_FORMAT_R32_FLOAT},
    {"fsr3 dilated motion vectors", L"FSR 3 dilated motion vectors", VK_FORMAT_R16G16_SFLOAT,
     FFX_SURFACE_FORMAT_R16G16_FLOAT},
    {"fsr3 previous nearest depth", L"FSR 3 previous nearest depth", VK_FORMAT_R32_UINT, FFX_SURFACE_FORMAT_R32_UINT},
};

}  // namespace

struct Fsr3::Impl {
    const vk::Device& device;
    VkDeviceContext deviceContext{};   // (the SDK keeps a pointer to it: it lives as long as the context)
    std::vector<unsigned char> scratch;
    FfxFsr3UpscalerContext context{};
    bool created = false;
    VkExtent2D display{};
    vk::Image shared[3];
    bool sharedCleared = false;   // cleared to 0 at the first dispatch, out of UNDEFINED (lane B)
    explicit Impl(const vk::Device& d) : device(d) {}
};

namespace {

FfxResource resource(vk::Image& image, VkImageUsageFlags usage, const wchar_t* name, FfxResourceStates state) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = image.format;
    info.extent = {image.extent.width, image.extent.height, 1};
    info.mipLevels = image.mips;
    info.arrayLayers = image.layers;
    info.samples = image.samples;
    info.usage = usage;
    return ffxGetResourceVK(reinterpret_cast<void*>(image.image), ffxGetImageResourceDescriptionVK(image.image, info),
                            name, state);
}

constexpr VkImageUsageFlags kRead = VK_IMAGE_USAGE_SAMPLED_BIT;
constexpr VkImageUsageFlags kReadWrite = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

}  // namespace

Fsr3::Fsr3(const vk::Device& device) : impl(std::make_unique<Impl>(device)) {}
Fsr3::~Fsr3() { destroy(); }

bool Fsr3::create(VkExtent2D displaySize) {
    destroy();
    // The context's size as gt4_fsr3 was built: a file here without the forced header would allocate the SDK's
    // default and overflow on a 4-byte wchar_t (ffx3_compat.h; lane D)
    if (sizeof(FfxFsr3UpscalerContext) != gt4Fsr3ContextSize()) {
        std::fprintf(stderr, "FSR 3: context size %zu here, %zu in gt4_fsr3: not run\n", sizeof(FfxFsr3UpscalerContext),
                     gt4Fsr3ContextSize());
        return false;
    }
    g_pipelineCache = impl->device.pipelineCache;
    impl->deviceContext = {impl->device.device, impl->device.physical, &deviceProcAddr};
    impl->scratch.assign(ffxGetScratchMemorySizeVK(impl->device.physical, FFX_FSR3UPSCALER_CONTEXT_COUNT), 0);
    FfxFsr3UpscalerContextDescription description{};
    FfxErrorCode error = ffxGetInterfaceVK(&description.backendInterface, ffxGetDeviceVK(&impl->deviceContext),
                                           impl->scratch.data(), impl->scratch.size(), FFX_FSR3UPSCALER_CONTEXT_COUNT);
    if (error != FFX_OK) {
        std::fprintf(stderr, "FSR 3: backend interface failed (%d)\n", static_cast<int>(error));
        return false;
    }
    // As FSR 2 (CONTRACTS b.11): HDR colour, inverted depth with an infinite far plane; render-resolution motion
    // vectors without jitter, as fsr3_shaders.cpp's permutations are built.
    description.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED |
                        FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE;
    description.maxRenderSize = {displaySize.width, displaySize.height};
    description.maxUpscaleSize = {displaySize.width, displaySize.height};
    description.fpMessage = message;
    error = ffxFsr3UpscalerContextCreate(&impl->context, &description);
    if (error != FFX_OK) {
        std::fprintf(stderr, "FSR 3: context creation failed (%d)\n", static_cast<int>(error));
        return false;
    }
    impl->created = true;
    impl->display = displaySize;

    // The shared images, ours (one allocator, named), at the size the SDK describes, kept across frames
    FfxFsr3UpscalerSharedResourceDescriptions descriptions{};
    ffxFsr3UpscalerGetSharedResourceDescriptions(&impl->context, &descriptions);
    const FfxCreateResourceDescription* wanted[3] = {&descriptions.dilatedDepth, &descriptions.dilatedMotionVectors,
                                                     &descriptions.reconstructedPrevNearestDepth};
    uint64_t sharedBytes = 0;
    for (int i = 0; i < 3; ++i) {
        const FfxResourceDescription& d = wanted[i]->resourceDescription;
        if (d.format != kShared[i].expected || d.width == 0 || d.height == 0) {
            std::fprintf(stderr, "FSR 3: shared image %s described as format %d, %ux%u: not run\n", kShared[i].name,
                         static_cast<int>(d.format), d.width, d.height);
            destroy();
            return false;
        }
        vk::ImageDesc desc;
        desc.format = kShared[i].format;
        desc.extent = {d.width, d.height};
        desc.usage = kReadWrite | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.name = kShared[i].name;
        impl->shared[i] = vk::createImage(impl->device, desc);
        sharedBytes += uint64_t(d.width) * d.height * 4u;   // (all three are 4 bytes a texel)
    }
    impl->sharedCleared = false;

    // Its memory, for C6b's report (the Odin's budget)
    FfxEffectMemoryUsage usage{};
    if (ffxFsr3UpscalerContextGetGpuMemoryUsage(&impl->context, &usage) == FFX_OK)
        std::printf("FSR 3: %ux%u: context %.1f MiB (%.1f MiB aliasable), shared images %.1f MiB, pipeline cache %s\n",
                    displaySize.width, displaySize.height, usage.totalUsageInBytes / 1048576.0,
                    usage.aliasableUsageInBytes / 1048576.0, sharedBytes / 1048576.0,
                    g_pipelineCache != VK_NULL_HANDLE ? "ours" : "none");
    std::fflush(stdout);
    return true;
}

void Fsr3::destroy() {
    if (!impl->created) return;
    vkDeviceWaitIdle(impl->device.device);
    ffxFsr3UpscalerContextDestroy(&impl->context);
    for (vk::Image& image : impl->shared)
        if (image.valid()) vk::destroy(impl->device, image);
    impl->created = false;
}

bool Fsr3::valid() const { return impl->created; }
VkExtent2D Fsr3::displaySize() const { return impl->display; }

void Fsr3::dispatch(VkCommandBuffer command, const upscale::Inputs& in) {
    vk::Barriers barriers;
    // The shared images' first use: out of UNDEFINED through a clear to 0, since the first frame and a reset frame
    // may read them (lane B)
    if (!impl->sharedCleared) {
        for (vk::Image& image : impl->shared)
            barriers.image(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           true);
        barriers.flush(command);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (vk::Image& image : impl->shared)
            vkCmdClearColorImage(command, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
        impl->sharedCleared = true;
    }
    // Declared to the SDK as they are (lane B): the inputs compute-read, the output and the shared images unordered
    // access in GENERAL; the backend returns them to those states at the end of its dispatch.
    vk::toSampled(barriers, *in.color, vk::kCompute);
    vk::toSampled(barriers, *in.depth, vk::kCompute);
    vk::toSampled(barriers, *in.velocity, vk::kCompute);
    vk::toSampled(barriers, *in.exposure, vk::kCompute);
    for (vk::Image* mask : {in.reactive, in.transparencyAndComposition})
        if (mask) vk::toSampled(barriers, *mask, vk::kCompute);
    vk::toStorage(barriers, *in.output, true);
    for (vk::Image& image : impl->shared) vk::toStorage(barriers, image, false);   // (kept: read the frame after)
    barriers.flush(command);

    FfxFsr3UpscalerDispatchDescription d{};
    d.commandList = ffxGetCommandListVK(command);
    d.color = resource(*in.color, kRead, L"FSR 3 colour", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.depth = resource(*in.depth, kRead, L"FSR 3 depth", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = resource(*in.velocity, kRead, L"FSR 3 motion vectors", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.exposure = resource(*in.exposure, kRead, L"FSR 3 exposure", FFX_RESOURCE_STATE_COMPUTE_READ);
    if (in.reactive) d.reactive = resource(*in.reactive, kRead, L"FSR 3 reactive", FFX_RESOURCE_STATE_COMPUTE_READ);
    if (in.transparencyAndComposition)
        d.transparencyAndComposition = resource(*in.transparencyAndComposition, kRead, L"FSR 3 transparency",
                                                FFX_RESOURCE_STATE_COMPUTE_READ);
    d.dilatedDepth = resource(impl->shared[0], kReadWrite, kShared[0].wideName, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.dilatedMotionVectors =
        resource(impl->shared[1], kReadWrite, kShared[1].wideName, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.reconstructedPrevNearestDepth =
        resource(impl->shared[2], kReadWrite, kShared[2].wideName, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.output = resource(*in.output, kReadWrite, L"FSR 3 output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset = {in.jitter.x, in.jitter.y};
    // Motion vectors are stored in uv units; FSR 3 divides the scale by the render size (as FSR 2)
    d.motionVectorScale = {static_cast<float>(in.renderSize.width), static_cast<float>(in.renderSize.height)};
    d.renderSize = {in.renderSize.width, in.renderSize.height};
    d.upscaleSize = {impl->display.width, impl->display.height};
    d.enableSharpening = in.sharpness > 0.0f;
    d.sharpness = in.sharpness;
    d.frameTimeDelta = in.deltaMilliseconds;
    d.preExposure = 1.0f;
    d.reset = in.reset;
    d.cameraNear = in.nearPlane;
    d.cameraFar = 1.0e6f;   // infinite far plane (FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE)
    d.cameraFovAngleVertical = in.verticalFov;
    d.viewSpaceToMetersFactor = 1.0f;
    const FfxErrorCode error = ffxFsr3UpscalerContextDispatch(&impl->context, &d);
    if (error != FFX_OK) std::fprintf(stderr, "FSR 3: dispatch failed (%d)\n", static_cast<int>(error));
    // The states the backend leaves, exactly as declared above
    for (vk::Image* image : {in.color, in.depth, in.velocity, in.exposure, in.reactive, in.transparencyAndComposition}) {
        if (!image) continue;
        image->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image->stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        image->access = VK_ACCESS_2_SHADER_READ_BIT;
    }
    for (vk::Image* image : {in.output, &impl->shared[0], &impl->shared[1], &impl->shared[2]}) {
        image->layout = VK_IMAGE_LAYOUT_GENERAL;
        image->stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        image->access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    }
}

int Fsr3::phaseCount(uint32_t renderWidth, uint32_t displayWidth) {
    return ffxFsr3UpscalerGetJitterPhaseCount(static_cast<int32_t>(renderWidth), static_cast<int32_t>(displayWidth));
}

Vec2 Fsr3::offset(int index, int phases) {
    Vec2 result;
    ffxFsr3UpscalerGetJitterOffset(&result.x, &result.y, index, phases);
    return result;
}

}  // namespace gt4
