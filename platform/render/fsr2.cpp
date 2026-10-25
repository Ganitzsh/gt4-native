#include "fsr2.h"

#include "ffx_fsr2.h"
#include "vk/ffx_fsr2_vk.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace gt4 {

struct Fsr2::Impl {
    const vk::Device& device;
    FfxFsr2Context context{};
    std::vector<unsigned char> scratch;
    bool created = false;
    VkExtent2D display{};
    explicit Impl(const vk::Device& d) : device(d) {}
};

namespace {
void message(FfxFsr2MsgType type, const wchar_t* text) {
    std::fprintf(stderr, "FSR2 %s: %ls\n", type == FFX_FSR2_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
}

FfxResource resource(FfxFsr2Context* context, vk::Image& image, const wchar_t* name, FfxResourceStates state) {
    return ffxGetTextureResourceVK(context, image.image, image.view, image.extent.width, image.extent.height,
                                   image.format, name, state);
}
}  // namespace

Fsr2::Fsr2(const vk::Device& device) : impl(std::make_unique<Impl>(device)) {}
Fsr2::~Fsr2() { destroy(); }

bool Fsr2::create(VkExtent2D displaySize) {
    destroy();
    impl->scratch.assign(ffxFsr2GetScratchMemorySizeVK(impl->device.physical), 0);
    FfxFsr2ContextDescription description{};
    FfxErrorCode error = ffxFsr2GetInterfaceVK(&description.callbacks, impl->scratch.data(), impl->scratch.size(),
                                               impl->device.physical, vkGetDeviceProcAddr);
    if (error != FFX_OK) {
        std::fprintf(stderr, "FSR2: backend interface failed (%d)\n", static_cast<int>(error));
        return false;
    }
    description.device = ffxGetDeviceVK(impl->device.device);
    description.maxRenderSize = {displaySize.width, displaySize.height};
    description.displaySize = {displaySize.width, displaySize.height};
    description.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_DEPTH_INFINITE;
    description.fpMessage = message;
    error = ffxFsr2ContextCreate(&impl->context, &description);
    if (error != FFX_OK) {
        std::fprintf(stderr, "FSR2: context creation failed (%d)\n", static_cast<int>(error));
        return false;
    }
    impl->created = true;
    impl->display = displaySize;
    return true;
}

void Fsr2::destroy() {
    if (!impl->created) return;
    vkDeviceWaitIdle(impl->device.device);
    ffxFsr2ContextDestroy(&impl->context);
    impl->created = false;
}

bool Fsr2::valid() const { return impl->created; }
VkExtent2D Fsr2::displaySize() const { return impl->display; }

void Fsr2::dispatch(VkCommandBuffer command, const Fsr2Inputs& in) {
    // Inputs in SHADER_READ_ONLY (FSR2 "compute read"), the output in GENERAL ("unordered access").
    vk::Barriers barriers;
    vk::toSampled(barriers, *in.color, vk::kCompute);
    vk::toSampled(barriers, *in.depth, vk::kCompute);
    vk::toSampled(barriers, *in.velocity, vk::kCompute);
    vk::toSampled(barriers, *in.exposure, vk::kCompute);
    vk::toStorage(barriers, *in.output, true);
    barriers.flush(command);
    FfxFsr2DispatchDescription d{};
    d.commandList = ffxGetCommandListVK(command);
    d.color = resource(&impl->context, *in.color, L"FSR2 colour", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.depth = resource(&impl->context, *in.depth, L"FSR2 depth", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = resource(&impl->context, *in.velocity, L"FSR2 motion vectors", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.exposure = resource(&impl->context, *in.exposure, L"FSR2 exposure", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.output = resource(&impl->context, *in.output, L"FSR2 output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset = {in.jitter.x, in.jitter.y};
    // Motion vectors are stored in uv units; FSR2 divides the scale by the render size.
    d.motionVectorScale = {static_cast<float>(in.renderSize.width), static_cast<float>(in.renderSize.height)};
    d.renderSize = {in.renderSize.width, in.renderSize.height};
    d.enableSharpening = in.sharpness > 0.0f;
    d.sharpness = in.sharpness;
    d.frameTimeDelta = in.deltaMilliseconds;
    d.preExposure = 1.0f;
    d.reset = in.reset;
    d.cameraNear = in.near;
    d.cameraFar = 1.0e6f;   // infinite far plane (FFX_FSR2_ENABLE_DEPTH_INFINITE)
    d.cameraFovAngleVertical = in.verticalFov;
    d.viewSpaceToMetersFactor = 1.0f;
    const FfxErrorCode error = ffxFsr2ContextDispatch(&impl->context, &d);
    if (error != FFX_OK) std::fprintf(stderr, "FSR2: dispatch failed (%d)\n", static_cast<int>(error));
    // The backend leaves the inputs readable and the output in GENERAL; record that for our tracking.
    for (vk::Image* image : {in.color, in.depth, in.velocity, in.exposure}) {
        image->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image->stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        image->access = VK_ACCESS_2_SHADER_READ_BIT;
    }
    in.output->layout = VK_IMAGE_LAYOUT_GENERAL;
    in.output->stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    in.output->access = VK_ACCESS_2_SHADER_WRITE_BIT;
}

int Fsr2::jitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth) {
    return ffxFsr2GetJitterPhaseCount(static_cast<int32_t>(renderWidth), static_cast<int32_t>(displayWidth));
}

Vec2 Fsr2::jitterOffset(int index, int phaseCount) {
    Vec2 result;
    ffxFsr2GetJitterOffset(&result.x, &result.y, index, phaseCount);
    return result;
}

}  // namespace gt4
