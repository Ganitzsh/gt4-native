// The PS2 displayed frame as the pipeline's scene (see gs_scene.h).
#include "gs_scene.h"

#include "gt4_shaders.h"

#include <array>
#include <cstring>
#include <vector>

namespace gt4 {

namespace {
constexpr uint32_t kSlots = 2;   // frames in flight (renderer.cpp)
}

struct GsScene::Impl {
    const vk::Device& device;
    VkCommandPool pool;
    vk::Pipeline pipeline;
    SceneFormats formats;
    VkSampler sampler = VK_NULL_HANDLE;
    vk::Image texture;
    std::array<vk::Buffer, kSlots> staging{};
    std::vector<uint32_t> pending;
    VkExtent2D pendingSize{}, size{};
    bool dirty = false;

    Impl(const vk::Device& d, VkCommandPool p) : device(d), pool(p) {
        sampler = vk::createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    }
    ~Impl() {
        vkDeviceWaitIdle(device.device);
        vk::destroy(device, pipeline);
        vk::destroy(device, texture);
        for (auto& b : staging) vk::destroy(device, b);
        if (sampler) vkDestroySampler(device.device, sampler, nullptr);
    }

    void ensureTexture(VkExtent2D extent) {
        if (texture.valid() && texture.extent.width == extent.width && texture.extent.height == extent.height) return;
        vkDeviceWaitIdle(device.device);   // the previous texture may still be sampled by a frame in flight
        vk::destroy(device, texture);
        for (auto& b : staging) vk::destroy(device, b);
        vk::ImageDesc desc;
        desc.format = VK_FORMAT_R8G8B8A8_SRGB;
        desc.extent = extent;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        desc.name = "gs frame";
        texture = vk::createImage(device, desc);
        for (auto& b : staging)
            b = vk::createBuffer(device, VkDeviceSize(extent.width) * extent.height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    }
};

GsScene::GsScene(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {}
GsScene::~GsScene() = default;

void GsScene::setFrame(uint32_t width, uint32_t height, const uint32_t* rgba) {
    if (!width || !height || !rgba) return;
    impl->pending.assign(rgba, rgba + size_t(width) * height);
    impl->pendingSize = {width, height};
    impl->dirty = true;
}

bool GsScene::hasFrame() const { return impl->texture.valid() || impl->dirty; }
VkExtent2D GsScene::frameSize() const { return impl->dirty ? impl->pendingSize : impl->size; }

void GsScene::prepare(const SceneFormats& formats) {
    Impl& s = *impl;
    vkDeviceWaitIdle(s.device.device);
    vk::destroy(s.device, s.pipeline);
    s.formats = formats;
    vk::GraphicsDesc desc;
    desc.vertex = shaders::fullscreen_vert();
    desc.fragment = shaders::gs_frame_frag();
    desc.colors = {formats.color, formats.velocity};
    desc.depth = formats.depth;
    desc.samples = formats.samples;
    desc.types = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    desc.name = "gs frame";
    s.pipeline = vk::createGraphics(s.device, desc);
}

void GsScene::recordPrepass(VkCommandBuffer command, const SceneView& view) {
    Impl& s = *impl;
    if (!s.dirty) return;
    s.ensureTexture(s.pendingSize);
    vk::Buffer& buffer = s.staging[view.slot % kSlots];
    std::memcpy(buffer.mapped, s.pending.data(), s.pending.size() * 4);
    vk::Barriers b;
    b.image(s.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
    b.flush(command);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {s.pendingSize.width, s.pendingSize.height, 1};
    vkCmdCopyBufferToImage(command, buffer.buffer, s.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    vk::toSampled(b, s.texture, vk::kFragment);
    b.flush(command);
    s.size = s.pendingSize;
    s.dirty = false;
}

void GsScene::recordMain(VkCommandBuffer command, const SceneView& view) {
    Impl& s = *impl;
    if (!s.texture.valid() || !s.pipeline.valid()) return;   // nothing displayed yet: the cleared (black) targets
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(view.renderExtent.width), static_cast<float>(view.renderExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, view.renderExtent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vk::bind(command, s.pipeline);
    vk::Descriptors().sampled(0, s.texture.view, s.sampler).push(command, s.pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
}

SceneCamera GsScene::camera() const {
    SceneCamera camera;   // identity view and projection: the frame is already 2D
    camera.previousViewProjection = camera.projection * camera.view;
    return camera;
}

}  // namespace gt4
