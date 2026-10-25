#include "renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": Vulkan result " + std::to_string(result));
}
template <typename T>
T load(PFN_vkGetInstanceProcAddr get, VkInstance instance, const char* name) {
    auto function = reinterpret_cast<T>(get(instance, name));
    if (!function) throw std::runtime_error(std::string("Missing Vulkan entry point: ") + name);
    return function;
}
#define DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateCommandPool) X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdClearAttachments) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkResetFences) X(vkAcquireNextImageKHR) X(vkQueueSubmit) X(vkQueuePresentKHR) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) \
    X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCmdPipelineBarrier) X(vkCmdCopyImageToBuffer)
}

struct Renderer::Impl {
    SDL_Window* window;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    uint32_t family;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkExtent2D extent{};
    bool resized = true;
    bool suspended = false;
    VkFormat pixelFormat = VK_FORMAT_UNDEFINED;
    bool canCapture = false;
    float horizontal = 0.0f;
    bool action = false;
    std::string capturePath;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    // Present waits are tied to swapchain images, not CPU frames. Reacquiring an image
    // guarantees its earlier present wait has finished before we signal this semaphore again.
    std::vector<VkSemaphore> finished;
#define DECLARE(name) PFN_##name name = nullptr;
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR capabilities = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR formats = nullptr;

    Impl(SDL_Window* window_, VkSurfaceKHR surface_, VkPhysicalDevice physical_, uint32_t family_)
        : window(window_), surface(surface_), physical(physical_), family(family_) {}
    ~Impl() {
        if (!device) return;
        if (vkDeviceWaitIdle) vkDeviceWaitIdle(device);
        releaseReadback();
        releaseSwapchain();
        if (submitted && vkDestroyFence) vkDestroyFence(device, submitted, nullptr);
        if (acquired && vkDestroySemaphore) vkDestroySemaphore(device, acquired, nullptr);
        if (pool && vkDestroyCommandPool) vkDestroyCommandPool(device, pool, nullptr);
        if (vkDestroyDevice) vkDestroyDevice(device, nullptr);
    }
    void initialize(VkInstance instance, PFN_vkGetInstanceProcAddr get) {
        auto memory = load<PFN_vkGetPhysicalDeviceMemoryProperties>(get, instance, "vkGetPhysicalDeviceMemoryProperties");
        memory(physical, &memoryProperties);
        capabilities = load<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(get, instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        formats = load<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(get, instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        auto create = load<PFN_vkCreateDevice>(get, instance, "vkCreateDevice");
        auto getDevice = load<PFN_vkGetDeviceProcAddr>(get, instance, "vkGetDeviceProcAddr");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queueInfo;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        check(create(physical, &info, nullptr, &device), "vkCreateDevice");
#define LOAD(name) \
        name = reinterpret_cast<PFN_##name>(getDevice(device, #name)); \
        if (!name) throw std::runtime_error("Missing Vulkan device entry point: " #name);
        DEVICE_FUNCTIONS(LOAD)
#undef LOAD
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocation, &command), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device, &semaphore, nullptr, &acquired), "vkCreateSemaphore");
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fence, nullptr, &submitted), "vkCreateFence");
    }
    void releaseSwapchain() {
        for (auto framebuffer : framebuffers) if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffers.clear();
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
        for (auto view : views) if (view) vkDestroyImageView(device, view, nullptr);
        views.clear();
        for (auto semaphore : finished) if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
        finished.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    bool recreate() {
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") + SDL_GetError());
        if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;
        VkSurfaceCapabilitiesKHR caps{};
        check(capabilities(physical, surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        extent = caps.currentExtent;
        if (extent.width == std::numeric_limits<uint32_t>::max()) {
            extent.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (!extent.width || !extent.height) return false;
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            throw std::runtime_error("Vulkan surface does not support color attachments");
        uint32_t count = 0;
        check(formats(physical, surface, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!count) throw std::runtime_error("Vulkan surface has no formats");
        std::vector<VkSurfaceFormatKHR> choices(count);
        check(formats(physical, surface, &count, choices.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        // Prefer sRGB so diagnostic colors have the same encoding on both platforms.
        auto chosen = choices[0];
        if (count == 1 && chosen.format == VK_FORMAT_UNDEFINED)
            chosen.format = VK_FORMAT_B8G8R8A8_SRGB;
        for (uint32_t i = 0; i < count; ++i)
            if (choices[i].format == VK_FORMAT_B8G8R8A8_SRGB && choices[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                chosen = choices[i];
        // This simple host has one submission in flight. Wait only for teardown/recreation,
        // not on every frame. FIFO presentation is guaranteed by Vulkan surface support.
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        releaseSwapchain();
        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount && info.minImageCount > caps.maxImageCount) info.minImageCount = caps.maxImageCount;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        canCapture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (canCapture) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        pixelFormat = chosen.format;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        for (auto alpha : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR })
            if (caps.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        std::printf("Creating swapchain: format %u, color space %u, alpha %u, extent %ux%u\n",
                    static_cast<unsigned>(chosen.format), static_cast<unsigned>(chosen.colorSpace),
                    static_cast<unsigned>(info.compositeAlpha), extent.width, extent.height);
        check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
        std::vector<VkImage> images(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "vkGetSwapchainImagesKHR");
        images.resize(count);
        swapImages = images;
        VkAttachmentDescription attachment{};
        attachment.format = chosen.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass.attachmentCount = 1;
        pass.pAttachments = &attachment;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 1;
        pass.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &pass, nullptr, &renderPass), "vkCreateRenderPass");
        views.resize(count);
        framebuffers.resize(count);
        finished.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = chosen.format;
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            check(vkCreateImageView(device, &view, nullptr, &views[i]), "vkCreateImageView");
            VkFramebufferCreateInfo framebuffer{};
            framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer.renderPass = renderPass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &views[i];
            framebuffer.width = extent.width;
            framebuffer.height = extent.height;
            framebuffer.layers = 1;
            check(vkCreateFramebuffer(device, &framebuffer, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
            VkSemaphoreCreateInfo semaphore{};
            semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            check(vkCreateSemaphore(device, &semaphore, nullptr, &finished[i]), "vkCreateSemaphore");
        }
        resized = false;
        std::printf("Vulkan swapchain: %ux%u, %u images\n", extent.width, extent.height, count);
        return true;
    }
    void releaseReadback() {
        if (readback) vkDestroyBuffer(device, readback, nullptr);
        readback = VK_NULL_HANDLE;
        if (readbackMemory) vkFreeMemory(device, readbackMemory, nullptr);
        readbackMemory = VK_NULL_HANDLE;
    }
    void recordReadback(uint32_t index) {
        if (!canCapture || (pixelFormat != VK_FORMAT_R8G8B8A8_UNORM && pixelFormat != VK_FORMAT_R8G8B8A8_SRGB &&
                            pixelFormat != VK_FORMAT_B8G8R8A8_UNORM && pixelFormat != VK_FORMAT_B8G8R8A8_SRGB))
            throw std::runtime_error("Framebuffer capture requires transfer-source support and an RGBA/BGRA8 surface");
        releaseReadback();
        VkBufferCreateInfo buffer{};
        buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer.size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &buffer, nullptr, &readback), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, readback, &requirements);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
        if (type == UINT32_MAX) throw std::runtime_error("No coherent host-visible memory for framebuffer capture");
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &allocation, nullptr, &readbackMemory), "vkAllocateMemory");
        check(vkBindBufferMemory(device, readback, readbackMemory, 0), "vkBindBufferMemory");
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapImages[index];
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { extent.width, extent.height, 1 };
        vkCmdCopyImageToBuffer(command, swapImages[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        VkBufferMemoryBarrier hostRead{};
        hostRead.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostRead.buffer = readback;
        hostRead.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &hostRead, 0, nullptr);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    void saveReadback() {
        void* mapped = nullptr;
        check(vkMapMemory(device, readbackMemory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        const auto* pixels = static_cast<const unsigned char*>(mapped);
        std::vector<unsigned char> rgb(static_cast<size_t>(extent.width) * extent.height * 3);
        const bool bgra = pixelFormat == VK_FORMAT_B8G8R8A8_UNORM || pixelFormat == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t i = 0; i < rgb.size() / 3; ++i) {
            rgb[i * 3] = pixels[i * 4 + (bgra ? 2 : 0)];
            rgb[i * 3 + 1] = pixels[i * 4 + 1];
            rgb[i * 3 + 2] = pixels[i * 4 + (bgra ? 0 : 2)];
        }
        vkUnmapMemory(device, readbackMemory);
        std::ofstream output(capturePath, std::ios::binary);
        output << "P6\n" << extent.width << " " << extent.height << "\n255\n";
        output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!output) throw std::runtime_error("Could not write framebuffer capture: " + capturePath);
        capturePath.clear();
        releaseReadback();
    }
    bool draw() {
        if (suspended || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) { resized = true; return false; }
        if (resized && !recreate()) return false;
        check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        uint32_t index = 0;
        // A finite acquire timeout allows the event loop to remain responsive when obscured.
        auto result = vkAcquireNextImageKHR(device, swapchain, 100000000, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_TIMEOUT || result == VK_NOT_READY) return false;
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { resized = true; return false; }
        const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        if (!suboptimal) check(result, "vkAcquireNextImageKHR");
        check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
        VkClearValue background{};
        background.color = {{0.018f, 0.026f, 0.045f, 1.0f}};
        VkRenderPassBeginInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = renderPass;
        pass.framebuffer = framebuffers[index];
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &background;
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        // A diagnostic pattern proves rendering/presentation without any game assets or shaders.
        // The green bar moves each frame, exposing frozen presentation and wrong resize extents.
        VkClearAttachment stripe{};
        stripe.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        stripe.colorAttachment = 0;
        stripe.clearValue.color = action ? VkClearColorValue{{0.8f, 0.12f, 0.025f, 1.0f}} : VkClearColorValue{{0.035f, 0.45f, 0.22f, 1.0f}};
        const uint32_t barWidth = std::max(1u, extent.width / 8);
        const uint32_t travel = extent.width - barWidth;
        const uint32_t offset = static_cast<uint32_t>((horizontal + 1.0f) * 0.5f * static_cast<float>(travel));
        VkClearRect rect{};
        rect.rect.offset = {static_cast<int32_t>(offset), static_cast<int32_t>(extent.height / 3)};
        rect.rect.extent = {barWidth, std::max(1u, extent.height / 3)};
        rect.layerCount = 1;
        vkCmdClearAttachments(command, 1, &stripe, 1, &rect);
        vkCmdEndRenderPass(command);
        if (!capturePath.empty()) recordReadback(index);
        check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submission{};
        submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submission.waitSemaphoreCount = 1;
        submission.pWaitSemaphores = &acquired;
        submission.pWaitDstStageMask = &stage;
        submission.commandBufferCount = 1;
        submission.pCommandBuffers = &command;
        submission.signalSemaphoreCount = 1;
        submission.pSignalSemaphores = &finished[index];
        // Reset only once a valid image and command buffer exist. An out-of-date acquire
        // must leave the fence signaled, otherwise the next frame would wait forever.
        check(vkResetFences(device, 1, &submitted), "vkResetFences");
        check(vkQueueSubmit(queue, 1, &submission, submitted), "vkQueueSubmit");
        if (!capturePath.empty()) {
            check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "vkWaitForFences(readback)");
            saveReadback();
        }
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &finished[index];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &index;
        result = vkQueuePresentKHR(queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal) resized = true;
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) check(result, "vkQueuePresentKHR");
        return result != VK_ERROR_OUT_OF_DATE_KHR;
    }
};

Renderer::Renderer(SDL_Window* window, VkInstance instance, VkSurfaceKHR surface,
                   VkPhysicalDevice physical, uint32_t family, PFN_vkGetInstanceProcAddr get)
    : impl(std::make_unique<Impl>(window, surface, physical, family)) {
    impl->initialize(instance, get);
}
Renderer::~Renderer() = default;
void Renderer::resize() { impl->resized = true; }
bool Renderer::draw() { return impl->draw(); }
VkExtent2D Renderer::drawableExtent() const { return impl->extent; }
void Renderer::setControls(float horizontal, bool action) {
    impl->horizontal = std::clamp(horizontal, -1.0f, 1.0f);
    impl->action = action;
}
void Renderer::capture(const std::string& path) {
    if (!impl->capturePath.empty()) throw std::runtime_error("A framebuffer capture is already pending");
    impl->capturePath = path;
}
void Renderer::suspend(bool paused) { impl->suspended = paused; impl->resized = true; }
