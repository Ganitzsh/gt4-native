// The render graph on a GPU (platform/render/graph/render_graph.h): four frames over both slots of a small graph
// (a transient cleared by dynamic rendering, one by a transfer, one aliased into the memory of another whose
// lifetime ended, a culled pass, a history read the next frame, a readback into an imported buffer), with
// Vulkan's validation and synchronization validation on when the layer is installed. Checks the pixels, that the
// culled pass never ran, the timestamps, and that validation said nothing. Skipped (77) without a Vulkan 1.3 GPU.
#include "render_graph.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace gt4;
using namespace gt4::rg;

int failures = 0;
int validationMessages = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                         const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        ++validationMessages;
        std::fprintf(stderr, "validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}

constexpr uint32_t kSize = 48;   // the output: three 16-pixel columns (A, B, last frame's history), E in the bottom rows
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

void clearImage(VkCommandBuffer command, vk::Image& image, float r, float g, float b) {
    VkClearColorValue value{};
    value.float32[0] = r;
    value.float32[1] = g;
    value.float32[2] = b;
    value.float32[3] = 1.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(command, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}

void copy(VkCommandBuffer command, vk::Image& from, vk::Image& to, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffset = {x, y, 0};
    region.extent = {w, h, 1};
    vkCmdCopyImage(command, from.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

}  // namespace

int main() {
    if (volkInitialize() != VK_SUCCESS) {
        std::puts("render_graph_gpu: no Vulkan loader; skipped");
        return 77;
    }
    // Instance, with validation and synchronization validation when the layer is there.
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    bool validation = false;
    for (const auto& l : layers) validation |= std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    const char* layerName = "VK_LAYER_KHRONOS_validation";
    const char* debugUtils = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkValidationFeatureEnableEXT enables[] = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    VkValidationFeaturesEXT features{};
    features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = enables;
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "gt4 render graph test";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    if (validation) {
        instanceInfo.enabledLayerCount = 1;
        instanceInfo.ppEnabledLayerNames = &layerName;
        instanceInfo.enabledExtensionCount = 1;
        instanceInfo.ppEnabledExtensionNames = &debugUtils;
        instanceInfo.pNext = &features;
    }
    vk::Device device;
    if (vkCreateInstance(&instanceInfo, nullptr, &device.instance) != VK_SUCCESS) {
        std::puts("render_graph_gpu: no Vulkan 1.3 instance; skipped");
        return 77;
    }
    volkLoadInstance(device.instance);
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        info.pfnUserCallback = onMessage;
        vkCreateDebugUtilsMessengerEXT(device.instance, &info, nullptr, &messenger);
    } else {
        std::puts("render_graph_gpu: VK_LAYER_KHRONOS_validation not installed; validation not checked");
    }

    // A Vulkan 1.3 GPU with dynamic rendering and synchronization2 (a discrete one first).
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(device.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(device.instance, &count, gpus.data());
    int best = -1;
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(gpus[i], &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        VkPhysicalDeviceVulkan13Features f13{};
        f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 f{};
        f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(gpus[i], &f);
        if (!f13.dynamicRendering || !f13.synchronization2) continue;
        if (best < 0 || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) best = static_cast<int>(i);
    }
    if (best < 0) {
        std::puts("render_graph_gpu: no Vulkan 1.3 GPU with dynamic rendering and synchronization2; skipped");
        if (messenger) vkDestroyDebugUtilsMessengerEXT(device.instance, messenger, nullptr);
        vkDestroyInstance(device.instance, nullptr);
        return 77;
    }
    device.physical = gpus[best];
    vkGetPhysicalDeviceProperties(device.physical, &device.properties);
    vkGetPhysicalDeviceMemoryProperties(device.physical, &device.memory);
    vkGetPhysicalDeviceQueueFamilyProperties(device.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device.physical, &count, families.data());
    for (uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { device.family = i; break; }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = device.family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &f13;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    vk::check(vkCreateDevice(device.physical, &deviceInfo, nullptr, &device.device), "vkCreateDevice");
    volkLoadDevice(device.device);
    vkGetDeviceQueue(device.device, device.family, 0, &device.queue);
    std::printf("render_graph_gpu: %s, validation %s\n", device.properties.deviceName, validation ? "on (synchronization too)" : "off");

    VkCommandPool pool;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device.family;
    vk::check(vkCreateCommandPool(device.device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");
    VkCommandBuffer commands[2];
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 2;
    vk::check(vkAllocateCommandBuffers(device.device, &allocate, commands), "vkAllocateCommandBuffers");
    VkFence fences[2];
    for (VkFence& f : fences) {
        VkFenceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vk::check(vkCreateFence(device.device, &info, nullptr, &f), "vkCreateFence");
    }

    // The imports: an output image per slot and a readback buffer per slot.
    vk::Image outputs[2];
    vk::Buffer readbacks[2];
    for (int s = 0; s < 2; ++s) {
        vk::ImageDesc desc;
        desc.format = kFormat;
        desc.extent = {kSize, kSize};
        desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.name = "graph test output";
        outputs[s] = vk::createImage(device, desc);
        readbacks[s] = vk::createBuffer(device, kSize * kSize * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    }

    {
        Graph graph(device);
        graph.setAbortOnError(false);
        bool culledRan = false;
        VkDeviceSize bytes = 0, bytesWithout = 0;
        bool aliased = false;
        bool valid[6] = {};   // historyValid() per frame (the history is made 32 x 32 from frame 4: new copies, not valid)
        // A frame's pixels and timings, once its slot's fence has signalled.
        auto checkFrame = [&](uint32_t frame) {
            const uint32_t slot = frame % 2;
            {
                const uint8_t* px = static_cast<const uint8_t*>(readbacks[slot].mapped);
                auto at = [&](uint32_t x, uint32_t y) { return px + (y * kSize + x) * 4; };
                const uint8_t red = static_cast<uint8_t>(40 + 30 * frame);
                check(at(8, 8)[0] == red && at(8, 8)[1] == 0, "frame " + std::to_string(frame) + ": A cleared by dynamic rendering");
                check(at(24, 8)[1] == 255 && at(24, 8)[0] == 0, "frame " + std::to_string(frame) + ": B cleared by a transfer");
                if (frame > 0 && valid[frame]) {
                    const uint8_t blue = static_cast<uint8_t>(20 + 40 * (frame - 1));
                    check(at(40, 8)[2] == blue, "frame " + std::to_string(frame) + ": the history holds the frame before's (" +
                                                    std::to_string(at(40, 8)[2]) + ", want " + std::to_string(blue) + ")");
                }
                check(at(8, 40)[0] == 255 && at(8, 40)[1] == 255 && at(8, 40)[2] == 0,
                      "frame " + std::to_string(frame) + ": E copied out before its memory went to A");
                const auto& t = graph.timings(slot);
                check(t.size() == 7, "timings: one per live pass (" + std::to_string(t.size()) + ")");
                bool gpuTimed = !t.empty();
                for (const auto& pass : t) gpuTimed &= pass.gpuMicroseconds >= 0.0;
                check(gpuTimed, "timings: GPU timestamps read back");
            }
        };
        for (uint32_t f = 0; f < 6; ++f) {
            const uint32_t slot = f % 2;
            vk::check(vkWaitForFences(device.device, 1, &fences[slot], VK_TRUE, UINT64_MAX), "vkWaitForFences");
            if (f >= 2) checkFrame(f - 2);   // the slot's last frame is done
            vk::check(vkResetFences(device.device, 1, &fences[slot]), "vkResetFences");

            FrameInfo info;
            info.slot = slot;
            info.frameIndex = f;
            info.render = info.output = {kSize, kSize};
            graph.begin(info);
            Texture out = graph.import(outputs[slot], "output");
            Buffer readback = graph.import(readbacks[slot], "readback");
            const uint32_t side = f < 4 ? 16 : 32;
            Texture history = graph.history("graph test history", TextureDesc{kFormat, Size::Absolute, 1, 1, {side, side}, 1, 1,
                                                                               VK_SAMPLE_COUNT_1_BIT, false, "history"});
            Texture e, a, b;
            const Texture previous = history;   // (the history pass below moves `history` on to version 1)
            const TextureDesc third{kFormat, Size::Render, 1.0f / 3.0f, 1.0f / 3.0f, {}, 1, 1, VK_SAMPLE_COUNT_1_BIT, false, "third"};
            graph.pass("clear E", [&](PassBuilder& p) { e = p.use(p.create(third), Use::TransferDst, VK_PIPELINE_STAGE_2_CLEAR_BIT); },
                       [&](PassContext& c) { clearImage(c.command, c.image(e), 1.0f, 1.0f, 0.0f); });
            graph.pass("E out", [&](PassBuilder& p) {
                p.use(e, Use::TransferSrc, VK_PIPELINE_STAGE_2_COPY_BIT);
                out = p.use(out, Use::TransferDst, VK_PIPELINE_STAGE_2_COPY_BIT);
            }, [&](PassContext& c) { copy(c.command, c.image(e), c.image(out), 0, 32, 16, 16); });
            graph.pass("draw A", [&](PassBuilder& p) { a = p.use(p.create(third), Use::ColorWrite, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT); },
                       [&, f](PassContext& c) {
                           VkClearValue clear{};
                           clear.color.float32[0] = (40.0f + 30.0f * f) / 255.0f;
                           clear.color.float32[3] = 1.0f;
                           c.beginRendering({a}, {}, &clear);
                           c.endRendering();
                       });
            graph.pass("clear B", [&](PassBuilder& p) { b = p.use(p.create(third), Use::TransferDst, VK_PIPELINE_STAGE_2_CLEAR_BIT); },
                       [&](PassContext& c) { clearImage(c.command, c.image(b), 0.0f, 1.0f, 0.0f); });
            graph.pass("never read", [&](PassBuilder& p) { p.use(p.create(third), Use::TransferDst, VK_PIPELINE_STAGE_2_CLEAR_BIT); },
                       [&](PassContext&) { culledRan = true; });
            graph.pass("compose", [&](PassBuilder& p) {
                p.use(a, Use::TransferSrc, VK_PIPELINE_STAGE_2_COPY_BIT);
                p.use(b, Use::TransferSrc, VK_PIPELINE_STAGE_2_COPY_BIT);
                p.use(history, Use::TransferSrc, VK_PIPELINE_STAGE_2_COPY_BIT);
                out = p.use(out, Use::TransferDst, VK_PIPELINE_STAGE_2_COPY_BIT);
            }, [&, f](PassContext& c) {
                copy(c.command, c.image(a), c.image(out), 0, 0, 16, 16);
                copy(c.command, c.image(b), c.image(out), 16, 0, 16, 16);
                if (f > 0) copy(c.command, c.image(previous), c.image(out), 32, 0, 16, 16);
            });
            const Texture input = history;   // (written from version 0: the pass must give the new copy for it)
            graph.pass("history", [&](PassBuilder& p) { history = p.use(history, Use::TransferDst, VK_PIPELINE_STAGE_2_CLEAR_BIT); },
                       [&, f, input](PassContext& c) { clearImage(c.command, c.image(input), 0.0f, 0.0f, (20.0f + 40.0f * f) / 255.0f); });
            graph.pass("readback", [&](PassBuilder& p) {
                p.use(out, Use::TransferSrc, VK_PIPELINE_STAGE_2_COPY_BIT);
                readback = p.use(readback, BufferUse::TransferDst, VK_PIPELINE_STAGE_2_COPY_BIT);
            }, [&](PassContext& c) {
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {kSize, kSize, 1};
                vkCmdCopyImageToBuffer(c.command, c.image(out).image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c.buffer(readback), 1, &region);
            });
            graph.compile();
            check(graph.plan().ok(), "frame " + std::to_string(f) + ": compiles");
            valid[f] = graph.historyValid(history);
            check(valid[f] == (f != 0 && f != 4), "frame " + std::to_string(f) + ": history valid " + std::to_string(valid[f]));
            if (!graph.plan().ok()) break;
            for (const Placement& pe : graph.plan().placements)
                for (const Placement& pa : graph.plan().placements)
                    if (pe.resource == e.id && pa.resource == a.id) aliased = pe.offset == pa.offset;
            bytes = graph.plan().transientBytes;
            bytesWithout = graph.plan().transientBytesWithoutAliasing;

            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vk::check(vkBeginCommandBuffer(commands[slot], &begin), "vkBeginCommandBuffer");
            graph.execute(commands[slot], VK_NULL_HANDLE);
            // The host reads the readback buffer after the fence.
            VkMemoryBarrier2 host{};
            host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &host;
            vkCmdPipelineBarrier2(commands[slot], &dependency);
            vk::check(vkEndCommandBuffer(commands[slot]), "vkEndCommandBuffer");
            VkCommandBufferSubmitInfo cmd{};
            cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmd.commandBuffer = commands[slot];
            VkSubmitInfo2 submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &cmd;
            vk::check(vkQueueSubmit2(device.queue, 1, &submit, fences[slot]), "vkQueueSubmit2");
        }
        vkDeviceWaitIdle(device.device);
        checkFrame(4);   // the last two frames (the history made again at frame 4, valid again at 5)
        checkFrame(5);
        check(!culledRan, "culling: the culled pass never ran");
        check(aliased, "aliasing: E's memory reused for A");
        std::printf("render_graph_gpu: transient memory %llu KB with aliasing, %llu KB without\n",
                    static_cast<unsigned long long>(bytes / 1024), static_cast<unsigned long long>(bytesWithout / 1024));
        graph.destroy();
    }

    for (int s = 0; s < 2; ++s) {
        vk::destroy(device, outputs[s]);
        vk::destroy(device, readbacks[s]);
        vkDestroyFence(device.device, fences[s], nullptr);
    }
    vkDestroyCommandPool(device.device, pool, nullptr);
    vkDestroyDevice(device.device, nullptr);
    if (messenger) vkDestroyDebugUtilsMessengerEXT(device.instance, messenger, nullptr);
    vkDestroyInstance(device.instance, nullptr);
    check(validationMessages == 0, "validation: " + std::to_string(validationMessages) + " message(s)");
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("render_graph_gpu: all checks passed\n");
    return failures ? 1 : 0;
}
