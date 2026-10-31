#include "renderer.h"
#include "../gs/cpu_profile.h"

#include "gsn_scene.h"

#include "display.h"

#include "overlay.h"
#include "pipeline.h"
#include "test_scene.h"
#include "vk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

using gt4::vk::check;

namespace {

constexpr uint32_t kFramesInFlight = 2;

std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

const char* typeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

std::string versionString(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

struct Candidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t index = 0;
    uint32_t family = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceDriverProperties driver{};
    std::string problem;   // empty when usable
    int score = 0;
};

bool hasExtension(VkPhysicalDevice device, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    for (const auto& e : extensions) if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// Checks one device; with a surface, also that it can really present to it (capabilities, formats and
// present modes all query successfully: a GPU cannot present to a display driven by another adapter).
Candidate examine(VkPhysicalDevice device, uint32_t index, VkSurfaceKHR surface) {
    Candidate c;
    c.device = device;
    c.index = index;
    c.driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &c.driver;
    vkGetPhysicalDeviceProperties2(device, &properties);
    c.properties = properties.properties;
    c.score = c.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
              c.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
    if (c.properties.apiVersion < VK_API_VERSION_1_3) { c.problem = "needs Vulkan 1.3 (has " + versionString(c.properties.apiVersion) + ")"; return c; }
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (!features13.dynamicRendering || !features13.synchronization2) { c.problem = "no dynamic rendering / synchronization2"; return c; }
    if (!hasExtension(device, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) { c.problem = "no VK_KHR_push_descriptor"; return c; }
    if (surface && !hasExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) { c.problem = "no VK_KHR_swapchain"; return c; }
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
    bool found = false;
    for (uint32_t i = 0; i < familyCount && !found; ++i) {
        const VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((families[i].queueFlags & need) != need || !families[i].queueCount) continue;
        if (surface) {
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present) != VK_SUCCESS || !present) continue;
        }
        c.family = i;
        found = true;
    }
    if (!found) { c.problem = surface ? "cannot present to this window's display" : "no graphics+compute queue"; return c; }
    if (surface) {
        VkSurfaceCapabilitiesKHR caps{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &caps);
        if (result != VK_SUCCESS) {
            c.problem = "surface capabilities failed (" + std::to_string(static_cast<int>(result)) +
                        "): the window is on a display this GPU does not drive";
            return c;
        }
        uint32_t count = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
        if (result != VK_SUCCESS || !count) { c.problem = "no surface formats"; return c; }
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr);
        if (result != VK_SUCCESS || !count) { c.problem = "no present modes"; return c; }
    }
    return c;
}

std::vector<Candidate> examineAll(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(count);
    if (count) check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
    std::vector<Candidate> result;
    for (uint32_t i = 0; i < count; ++i) result.push_back(examine(devices[i], i, surface));
    return result;
}

std::string label(const Candidate& c) {
    std::ostringstream out;
    out << "#" << c.index << " " << c.properties.deviceName << " (" << typeName(c.properties.deviceType) << ", Vulkan "
        << versionString(c.properties.apiVersion) << ", driver " << c.driver.driverName << " " << c.driver.driverInfo << ")";
    return out.str();
}

Candidate choose(VkInstance instance, VkSurfaceKHR surface, const std::string& request, SDL_Window* window) {
    auto candidates = examineAll(instance, surface);
    if (candidates.empty()) throw std::runtime_error("No Vulkan device found");
    for (const auto& c : candidates)
        std::printf("GPU %s: %s\n", label(c).c_str(), c.problem.empty() ? "usable" : c.problem.c_str());
    const Candidate* chosen = nullptr;
    const std::string want = lower(request);
    if (!want.empty() && want != "auto") {
        char* end = nullptr;
        const unsigned long index = std::strtoul(want.c_str(), &end, 10);
        for (const auto& c : candidates) {
            const bool match = (end && *end == '\0') ? c.index == index : lower(c.properties.deviceName).find(want) != std::string::npos;
            if (!match) continue;
            if (!c.problem.empty())
                throw std::runtime_error("Requested GPU " + label(c) + " cannot be used: " + c.problem +
                                         (surface ? ". The window is on " + gt4::windowDisplayName(window) +
                                                    "; move it to a display this GPU drives, or use --headless." : ""));
            chosen = &c;
            break;
        }
        if (!chosen) throw std::runtime_error("No GPU matches --gpu " + request + " (gt4_host --list-gpus)");
    } else {
        for (const auto& c : candidates)
            if (c.problem.empty() && (!chosen || c.score > chosen->score)) chosen = &c;
        if (!chosen)
            throw std::runtime_error(surface ?
                "No GPU can present to the window on " + gt4::windowDisplayName(window) + ". That display may be driven "
                "by an adapter without Vulkan presentation (for example the virtual display of a remote-streaming "
                "tool). Move the window to another monitor (display.monitor), turn a monitor on, or use --headless." :
                std::string("No usable Vulkan 1.3 GPU (see the list above)"));
    }
    std::printf("Using GPU %s\n", label(*chosen).c_str());
    return *chosen;
}

}  // namespace

std::vector<std::string> describeGpus(VkInstance instance, SDL_Window* window) {
    std::vector<std::string> lines;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (window && !SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) surface = VK_NULL_HANDLE;
    if (surface) lines.push_back("Presentation to the window on " + gt4::windowDisplayName(window) + ":");
    for (const auto& c : examineAll(instance, surface))
        lines.push_back("  " + label(c) + (c.problem.empty() ? " - usable" : " - " + c.problem));
    if (surface) {
        lines.push_back("Headless (no presentation):");
        for (const auto& c : examineAll(instance, VK_NULL_HANDLE))
            lines.push_back("  " + label(c) + (c.problem.empty() ? " - usable" : " - " + c.problem));
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }
    return lines;
}

struct Renderer::Impl {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    bool headless = false;
    gt4::vk::Device device;
    GpuInfo info;
    gt4::Settings settings;
    VkCommandPool pool = VK_NULL_HANDLE;
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        bool timed = false;
    };
    std::array<Frame, kFramesInFlight> frames{};
    uint32_t slot = 0;
    uint32_t frameCounter = 0;
    VkQueryPool timestamps = VK_NULL_HANDLE;
    bool timestampsSupported = false;
    double gpuMs = 0.0, gpuMsTotal = 0.0;
    uint64_t gpuSamples = 0;

    // Presentation target.
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<gt4::vk::Image> swapImages;     // views owned here, images by the swapchain
    std::vector<VkSemaphore> finished;
    gt4::vk::Image offscreen;
    VkExtent2D extent{};
    VkExtent2D headlessSize{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool srgbFormat = false;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool resized = true, suspended = false, displayMoved = false, sceneSwitched = false;
    gt4::VSync presentedVsync = gt4::VSync::On;
    int presentedBits = 8;
    bool presentedHdr = false;   // the swapchain is HDR10 (display.hdr, and the display offers it)
    bool wantedHdr = false;      // display.hdr as the swapchain was made

    std::unique_ptr<gt4::TestScene> scene;
    std::unique_ptr<gt4::GsnScene> gsnScene;   // the native renderer: the game's frames
    bool gameFrame = false;
    std::unique_ptr<gt4::FramePipeline> pipeline;
    std::unique_ptr<gt4::Overlay> overlay;
    float stick = 0.0f;
    bool action = false;
    std::string status;
    std::optional<gt4::MenuView> menu;
    std::string message;
    Uint64 messageUntil = 0;   // SDL_GetTicks
    std::string capturePath;
    gt4::vk::Buffer readback;

    ~Impl() {
        if (device.device) vkDeviceWaitIdle(device.device);
        overlay.reset();
        pipeline.reset();
        scene.reset();
        gsnScene.reset();
        if (!device.device) {
            if (surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
            return;
        }
        gt4::vk::destroy(device, readback);
        releaseTarget();
        for (auto& f : frames) {
            if (f.fence) vkDestroyFence(device.device, f.fence, nullptr);
            if (f.acquired) vkDestroySemaphore(device.device, f.acquired, nullptr);
        }
        if (timestamps) vkDestroyQueryPool(device.device, timestamps, nullptr);
        if (pool) vkDestroyCommandPool(device.device, pool, nullptr);
        vkDestroyDevice(device.device, nullptr);
        if (surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }

    void createDevice(const std::string& request) {
        const Candidate chosen = choose(instance, surface, request, window);
        device.instance = instance;
        device.physical = chosen.device;
        device.family = chosen.family;
        device.properties = chosen.properties;
        info.index = chosen.index;
        info.name = chosen.properties.deviceName;
        info.type = typeName(chosen.properties.deviceType);
        info.driver = std::string(chosen.driver.driverName) + " " + chosen.driver.driverInfo;
        info.api = versionString(chosen.properties.apiVersion);
        vkGetPhysicalDeviceMemoryProperties(device.physical, &device.memory);
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(device.physical, &supported);
        VkPhysicalDeviceVulkan13Features supported13{};
        supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 query{};
        query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query.pNext = &supported13;
        vkGetPhysicalDeviceFeatures2(device.physical, &query);

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features13.shaderDemoteToHelperInvocation = VK_TRUE;   // required by 1.3; glslang emits it for discard
        features13.subgroupSizeControl = supported13.subgroupSizeControl;   // FSR 2 may request wave64
        features13.computeFullSubgroups = supported13.computeFullSubgroups;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        features.features.samplerAnisotropy = supported.samplerAnisotropy;
        features.features.shaderStorageImageReadWithoutFormat = supported.shaderStorageImageReadWithoutFormat;
        features.features.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
        features.features.shaderImageGatherExtended = supported.shaderImageGatherExtended;
        features.features.dualSrcBlend = supported.dualSrcBlend;   // the native GS renderer's blends
        device.dualSourceBlend = supported.dualSrcBlend == VK_TRUE;
        device.anisotropy = supported.samplerAnisotropy == VK_TRUE;
        device.maxAnisotropy = device.anisotropy ? chosen.properties.limits.maxSamplerAnisotropy : 1.0f;
        device.sampleCounts = chosen.properties.limits.framebufferColorSampleCounts &
                              chosen.properties.limits.framebufferDepthSampleCounts;
        std::vector<const char*> extensions{VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
        if (surface) extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = device.family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create.pNext = &features;
        create.queueCreateInfoCount = 1;
        create.pQueueCreateInfos = &queue;
        create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        check(vkCreateDevice(device.physical, &create, nullptr, &device.device), "vkCreateDevice");
        volkLoadDevice(device.device);
        vkGetDeviceQueue(device.device, device.family, 0, &device.queue);
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device.family;
        check(vkCreateCommandPool(device.device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");
        for (auto& f : frames) {
            VkCommandBufferAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocation.commandPool = pool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device.device, &allocation, &f.command), "vkAllocateCommandBuffers");
            VkFenceCreateInfo fence{};
            fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device.device, &fence, nullptr, &f.fence), "vkCreateFence");
            VkSemaphoreCreateInfo semaphore{};
            semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            check(vkCreateSemaphore(device.device, &semaphore, nullptr, &f.acquired), "vkCreateSemaphore");
        }
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device.physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device.physical, &familyCount, families.data());
        timestampsSupported = families[device.family].timestampValidBits > 0 && device.properties.limits.timestampPeriod > 0.0f;
        if (timestampsSupported) {
            VkQueryPoolCreateInfo queries{};
            queries.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queries.queryCount = 2 * kFramesInFlight;
            check(vkCreateQueryPool(device.device, &queries, nullptr, &timestamps), "vkCreateQueryPool");
        }
        scene = std::make_unique<gt4::TestScene>(device, pool);
        gsnScene = std::make_unique<gt4::GsnScene>(device, pool);
        pipeline = std::make_unique<gt4::FramePipeline>(device, pool);
        overlay = std::make_unique<gt4::Overlay>(device, pool);
    }

    void releaseTarget() {
        for (auto& image : swapImages) if (image.view) vkDestroyImageView(device.device, image.view, nullptr);
        swapImages.clear();
        for (auto s : finished) if (s) vkDestroySemaphore(device.device, s, nullptr);
        finished.clear();
        if (swapchain) vkDestroySwapchainKHR(device.device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        gt4::vk::destroy(device, offscreen);
    }

    VkPresentModeKHR wantedPresentMode() const {
        switch (settings.vsync) {
        case gt4::VSync::Off: return VK_PRESENT_MODE_IMMEDIATE_KHR;
        case gt4::VSync::Adaptive: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        case gt4::VSync::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
        case gt4::VSync::On: break;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    void recreateSurface() {
        std::fprintf(stderr, "Vulkan surface lost; recreating it\n");
        vkDeviceWaitIdle(device.device);
        releaseTarget();
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
        VkBool32 present = VK_FALSE;
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device.physical, device.family, surface, &present) != VK_SUCCESS || !present ||
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical, surface, &caps) != VK_SUCCESS)
            throw PresentationLost("The window moved to " + gt4::windowDisplayName(window) + ", which " + info.name +
                                   " cannot present to");
    }

    // After the window changed display: the surface may now belong to an adapter this GPU does not drive.
    void checkPresentation() {
        displayMoved = false;
        VkBool32 present = VK_FALSE;
        VkSurfaceCapabilitiesKHR caps{};
        const bool ok = vkGetPhysicalDeviceSurfaceSupportKHR(device.physical, device.family, surface, &present) == VK_SUCCESS &&
                        present && vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical, surface, &caps) == VK_SUCCESS;
        if (!ok) recreateSurface();   // throws PresentationLost when the new surface is not presentable either
        resized = true;
    }

    bool recreateTarget() {
        if (headless) {
            if (offscreen.valid() && extent.width == headlessSize.width && extent.height == headlessSize.height &&
                presentedBits == settings.outputBits) { resized = false; return true; }
            vkDeviceWaitIdle(device.device);
            releaseTarget();
            gt4::vk::ImageDesc desc;
            desc.format = settings.outputBits == 10 ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_B8G8R8A8_UNORM;
            desc.extent = headlessSize;
            desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            desc.name = "headless output";
            offscreen = gt4::vk::createImage(device, desc);
            extent = headlessSize;
            format = desc.format;
            srgbFormat = false;
            presentedBits = settings.outputBits;
            overlay->prepare(format, srgbFormat);
            pipeline->prepareOutput(format, srgbFormat, static_cast<uint32_t>(settings.outputBits));
            resized = false;
            return true;
        }
        int width = 0, height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            throw std::runtime_error(std::string("SDL_GetWindowSizeInPixels: ") + SDL_GetError());
        if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;
        VkSurfaceCapabilitiesKHR caps{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical, surface, &caps);
        if (result != VK_SUCCESS) {
            // Surface lost, or the window is now on a display driven by another adapter (VK_ERROR_UNKNOWN on some
            // drivers): a new surface either works or raises PresentationLost.
            recreateSurface();
            result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical, surface, &caps);
        }
        if (result != VK_SUCCESS)
            throw PresentationLost("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed (" + std::to_string(static_cast<int>(result)) +
                                   ") on " + info.name + ": the window is on " + gt4::windowDisplayName(window) +
                                   ", which this GPU does not drive");
        VkExtent2D size = caps.currentExtent;
        if (size.width == std::numeric_limits<uint32_t>::max()) {
            size.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            size.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (!size.width || !size.height) return false;
        uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical, surface, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical, surface, &count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        // Preference: the pipeline writes sRGB-encoded values, so a UNORM format stores them exactly.
        std::vector<VkFormat> preferred;
        if (settings.outputBits == 10) preferred = {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32};
        for (VkFormat f : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB})
            preferred.push_back(f);
        VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
        bool found = false;
        // HDR10 (display.hdr): a 10-bit format in the ST 2084 colour space, when the display offers one.
        if (settings.hdr)
            for (VkFormat want : {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32}) {
                for (const auto& f : formats)
                    if (f.format == want && f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) { chosen = f; found = true; break; }
                if (found) break;
            }
        for (VkFormat want : preferred) {
            if (found) break;
            for (const auto& f : formats)
                if (f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; found = true; break; }
            if (found) break;
        }
        if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) chosen = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        uint32_t modeCount = 0;
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical, surface, &modeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        std::vector<VkPresentModeKHR> modes(modeCount);
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical, surface, &modeCount, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        presentMode = VK_PRESENT_MODE_FIFO_KHR;
        const VkPresentModeKHR want = wantedPresentMode();
        if (std::find(modes.begin(), modes.end(), want) != modes.end()) presentMode = want;
        else if (want == VK_PRESENT_MODE_IMMEDIATE_KHR && std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;   // no tearing available: at least do not wait
        check(vkDeviceWaitIdle(device.device), "vkDeviceWaitIdle");
        for (auto& image : swapImages) if (image.view) vkDestroyImageView(device.device, image.view, nullptr);
        swapImages.clear();
        for (auto s : finished) if (s) vkDestroySemaphore(device.device, s, nullptr);
        finished.clear();
        VkSwapchainKHR old = swapchain;
        VkSwapchainCreateInfoKHR create{};
        create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create.surface = surface;
        create.minImageCount = std::max(caps.minImageCount + 1, presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? 3u : 2u);
        if (caps.maxImageCount && create.minImageCount > caps.maxImageCount) create.minImageCount = caps.maxImageCount;
        create.imageFormat = chosen.format;
        create.imageColorSpace = chosen.colorSpace;
        create.imageExtent = size;
        create.imageArrayLayers = 1;
        create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) create.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.preTransform = caps.currentTransform;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & alpha) { create.compositeAlpha = alpha; break; }
        create.presentMode = presentMode;
        create.clipped = VK_TRUE;
        create.oldSwapchain = old;
        check(vkCreateSwapchainKHR(device.device, &create, nullptr, &swapchain), "vkCreateSwapchainKHR");
        if (old) vkDestroySwapchainKHR(device.device, old, nullptr);
        uint32_t imageCount = 0;
        check(vkGetSwapchainImagesKHR(device.device, swapchain, &imageCount, nullptr), "vkGetSwapchainImagesKHR");
        std::vector<VkImage> images(imageCount);
        check(vkGetSwapchainImagesKHR(device.device, swapchain, &imageCount, images.data()), "vkGetSwapchainImagesKHR");
        for (VkImage image : images) {
            gt4::vk::Image wrapper;
            wrapper.image = image;
            wrapper.format = chosen.format;
            wrapper.extent = size;
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = chosen.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device.device, &view, nullptr, &wrapper.view), "vkCreateImageView(swapchain)");
            swapImages.push_back(wrapper);
            VkSemaphoreCreateInfo semaphore{};
            semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkSemaphore s = VK_NULL_HANDLE;
            check(vkCreateSemaphore(device.device, &semaphore, nullptr, &s), "vkCreateSemaphore");
            finished.push_back(s);
        }
        extent = size;
        format = chosen.format;
        srgbFormat = chosen.format == VK_FORMAT_B8G8R8A8_SRGB || chosen.format == VK_FORMAT_R8G8B8A8_SRGB;
        presentedHdr = chosen.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
        wantedHdr = settings.hdr;
        if (settings.hdr && !presentedHdr) std::printf("HDR: the display offers no HDR10 format; SDR\n");
        presentedVsync = settings.vsync;
        presentedBits = settings.outputBits;
        overlay->prepare(format, srgbFormat);
        pipeline->prepareOutput(format, srgbFormat, chosen.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                                chosen.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 ? 10u : 8u);
        resized = false;
        std::printf("Swapchain %ux%u, format %d, present mode %d, %u images\n", size.width, size.height,
                    static_cast<int>(chosen.format), static_cast<int>(presentMode), imageCount);
        return true;
    }

    void composeOverlay(uint32_t frameSlot) {
        gt4::Canvas2D canvas;
        canvas.content = pipeline->contentRect();
        canvas.layout = settings.hud;
        canvas.safeArea = settings.safeArea;
        overlay->setFilter(settings.uiFilter);
        overlay->begin(frameSlot, extent, canvas);
        if (gameFrame) { composeReadout(); composeMessage(); composeMenu(); return; }
        // Test HUD on the 640 x 480 canvas, anchored like a racing game's.
        const gt4::Color8 panel{8, 12, 20, 170}, white{240, 240, 240, 255}, accent{255, 190, 40, 255};
        overlay->canvasRect(16, 16, 150, 44, panel, gt4::Anchor::Left);
        overlay->canvasText(24, 24, 12, "LAP " + std::to_string(scene->lap()) + "/5", white, gt4::Anchor::Left);
        overlay->canvasText(24, 42, 8, "BEST 1'23.456", accent, gt4::Anchor::Left);
        overlay->canvasRect(474, 16, 150, 44, panel, gt4::Anchor::Right);
        overlay->canvasText(616, 24, 12, "POS 1/6", white, gt4::Anchor::Right);
        overlay->canvasRect(474, 392, 150, 72, panel, gt4::Anchor::Right);
        char speed[32];
        std::snprintf(speed, sizeof(speed), "%3d", static_cast<int>(scene->speedKmh()));
        overlay->canvasText(600, 404, 32, speed, white, gt4::Anchor::Right);
        overlay->canvasText(616, 444, 8, "KM/H", accent, gt4::Anchor::Right);
        // Minimap placeholder and pixel-art sprites (texture.ui_filter: point keeps them crisp).
        overlay->canvasRect(16, 384, 80, 80, panel, gt4::Anchor::Left);
        overlay->canvasSprite(36, 404, 40, '@', accent, gt4::Anchor::Left);
        overlay->canvasText(320, 20, 8, "GT4 NATIVE - DISPLAY PIPELINE TEST SCENE", white, gt4::Anchor::Center);
        composeReadout();
        composeMessage();
        composeMenu();
    }

    // Integer text scale for the content height (the 8 x 8 font is point-sampled).
    float textScale(float lines) const {
        return std::max(1.0f, std::floor(static_cast<float>(pipeline->contentRect().extent.height) / lines));
    }

    void composeMessage() {
        if (message.empty() || SDL_GetTicks() >= messageUntil || menu) return;
        const auto content = pipeline->contentRect();
        const float scale = textScale(360.0f);
        const float w = 8.0f * scale * static_cast<float>(message.size()) + 16.0f * scale;
        const float h = 8.0f * scale + 12.0f * scale;
        const float x = static_cast<float>(content.offset.x) + (static_cast<float>(content.extent.width) - w) * 0.5f;
        const float y = static_cast<float>(content.offset.y + content.extent.height) - h - 24.0f * scale;
        overlay->pixelRect(x, y, w, h, {10, 14, 22, 200});
        overlay->pixelText(x + 8.0f * scale, y + 6.0f * scale, scale, message, {235, 238, 242, 255});
    }

    // The settings menu: a panel in the middle of the content, a scrolling list of rows, the selected row's
    // description and the controls underneath.
    void composeMenu() {
        if (!menu) return;
        const auto content = pipeline->contentRect();
        const float cx = static_cast<float>(content.offset.x), cy = static_cast<float>(content.offset.y);
        const float cw = static_cast<float>(content.extent.width), ch = static_cast<float>(content.extent.height);
        overlay->pixelRect(cx, cy, cw, ch, {0, 0, 0, 150});
        const float scale = textScale(400.0f);
        const float glyph = 8.0f * scale, rowHeight = 13.0f * scale, pad = 12.0f * scale;
        const size_t columns = 72;
        const float panelW = std::min(cw - 2.0f * pad, glyph * static_cast<float>(columns) + 2.0f * pad);
        const size_t fit = static_cast<size_t>(std::max(4.0f, (ch - 2.0f * pad - 8.0f * rowHeight) / rowHeight));
        const size_t shown = std::min(fit, menu->rows.size());
        const float panelH = rowHeight * static_cast<float>(shown) + 7.0f * rowHeight + 2.0f * pad;
        const float px = cx + (cw - panelW) * 0.5f, py = cy + std::max(pad, (ch - panelH) * 0.5f);
        const gt4::Color8 panel{12, 16, 26, 235}, text{225, 230, 238, 255}, dim{140, 150, 165, 255},
            accent{255, 196, 48, 255}, bar{48, 80, 140, 255}, ink{255, 255, 255, 255};
        overlay->pixelRect(px, py, panelW, panelH, panel);
        overlay->pixelRect(px, py, panelW, 2.0f * scale, accent);
        float y = py + pad;
        overlay->pixelText(px + pad, y, scale * 2.0f, menu->title, ink);
        y += 2.0f * rowHeight;
        // Scroll so that the selected row (and its section title) is visible.
        size_t selected = 0;
        for (size_t i = 0; i < menu->rows.size(); ++i) if (menu->rows[i].selected) selected = i;
        size_t first = 0;
        if (menu->rows.size() > shown) {
            const long want = static_cast<long>(selected > 0 && menu->rows[selected - 1].header ? selected - 1 : selected);
            first = static_cast<size_t>(std::clamp(want - static_cast<long>(shown / 2), 0L, static_cast<long>(menu->rows.size() - shown)));
        }
        const float listTop = y;
        const size_t width = static_cast<size_t>((panelW - 2.0f * pad) / glyph);
        for (size_t i = first; i < first + shown; ++i) {
            const auto& row = menu->rows[i];
            const float x = px + pad;
            if (row.header) {
                if (!row.label.empty()) overlay->pixelText(x, y + 2.0f * scale, scale, row.label, accent);
                y += rowHeight;
                continue;
            }
            if (row.selected) overlay->pixelRect(px + pad * 0.5f, y - 2.0f * scale, panelW - pad, rowHeight, bar);
            const std::string label = "  " + row.label + (row.restart ? " *" : "");
            overlay->pixelText(x, y + 1.0f * scale, scale, label, row.selected ? ink : text);
            if (row.adjustable) {
                const std::string value = row.selected ? "< " + row.value + " >" : row.value + "  ";
                overlay->pixelText(x + glyph * static_cast<float>(width - std::min(width, value.size())), y + 1.0f * scale, scale,
                                   value, row.selected ? ink : accent);
            }
            y += rowHeight;
        }
        if (first > 0) overlay->pixelText(px + panelW - pad - glyph, listTop - rowHeight, scale, "^", dim);
        if (first + shown < menu->rows.size()) overlay->pixelText(px + panelW - pad - glyph, y, scale, "v", dim);
        // The description, word-wrapped over three lines at most, then the controls.
        y += rowHeight;
        overlay->pixelRect(px + pad, y - 4.0f * scale, panelW - 2.0f * pad, 1.0f * scale, dim);
        std::string line, help = menu->help;
        int lines = 0;
        size_t start = 0;
        while (start < help.size() && lines < 3) {
            size_t end = std::min(help.size(), start + width);
            if (end < help.size()) {
                const size_t space = help.rfind(' ', end);
                if (space != std::string::npos && space > start) end = space;
            }
            overlay->pixelText(px + pad, y + rowHeight * static_cast<float>(lines), scale, help.substr(start, end - start), text);
            start = end + (end < help.size() && help[end] == ' ' ? 1 : 0);
            ++lines;
        }
        y += rowHeight * 3.5f;
        overlay->pixelText(px + pad, y, scale, menu->footer.substr(0, width), dim);
    }

    void composeReadout() {
        if (!settings.overlayText) return;
        const auto content = pipeline->contentRect();
        const float scale = std::max(1.0f, std::floor(static_cast<float>(content.extent.height) / 540.0f));
        std::ostringstream text;
        text << "preset " << gt4::name(settings.preset) << "   F1 hide  F2 preset  F3 AA  F4 upscaler  F5 quality\n"
             << "F6 aspect  F7 tonemap  F8 HUD  F9 vsync  F10 MSAA  F11 fullscreen  B N M H R F T Y [ ] - =\n"
             << pipeline->describe() << "\n"
             << "display " << (headless ? "headless" : gt4::name(settings.windowMode)) << " " << extent.width << "x" << extent.height
             << " vsync " << gt4::name(settings.vsync) << " aspect " << gt4::name(settings.aspect) << " hud " << gt4::name(settings.hud)
             << "\ntex " << gt4::name(settings.textureFilter) << " af " << settings.anisotropy << "x  tonemap "
             << gt4::name(settings.tonemapper) << "  bloom " << gt4::name(settings.bloom) << " dof " << gt4::name(settings.dof)
             << " mb " << gt4::name(settings.motionBlur) << "\nshadows " << gt4::name(settings.shadows) << " refl "
             << gt4::name(settings.reflections) << " fog " << gt4::name(settings.skyFog) << "  sharp " << settings.sharpness
             << "\nGPU " << info.name;
        if (gpuMs > 0.0) { char ms[32]; std::snprintf(ms, sizeof(ms), "  %.2f ms", gpuMs); text << ms; }
        if (!status.empty()) text << "\n" << status;
        const float x = static_cast<float>(content.offset.x) + 8.0f * scale;
        const float y = static_cast<float>(content.offset.y) + 72.0f * scale;
        const std::string lines = text.str();
        size_t rows = 1, columns = 0, column = 0;
        for (char c : lines) {
            if (c == '\n') { ++rows; column = 0; }
            else columns = std::max(columns, ++column);
        }
        overlay->pixelRect(x - 4 * scale, y - 4 * scale, 8 * scale * static_cast<float>(columns) + 8 * scale,
                           10 * scale * static_cast<float>(rows) + 6 * scale, {0, 0, 0, 140});
        overlay->pixelText(x, y, scale, lines, {230, 235, 240, 255});
    }

    void recordCapture(VkCommandBuffer command, gt4::vk::Image& target) {
        const bool supported = target.format == VK_FORMAT_B8G8R8A8_UNORM || target.format == VK_FORMAT_B8G8R8A8_SRGB ||
            target.format == VK_FORMAT_R8G8B8A8_UNORM || target.format == VK_FORMAT_R8G8B8A8_SRGB ||
            target.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || target.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        if (!supported) throw std::runtime_error("Capture does not support the output format " + std::to_string(static_cast<int>(target.format)));
        gt4::vk::destroy(device, readback);
        readback = gt4::vk::createBuffer(device, static_cast<VkDeviceSize>(target.extent.width) * target.extent.height * 4,
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        gt4::vk::Barriers b;
        b.image(target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, gt4::vk::kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT);
        b.flush(command);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {target.extent.width, target.extent.height, 1};
        vkCmdCopyImageToBuffer(command, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
        VkMemoryBarrier2 host{};
        host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        host.srcStageMask = gt4::vk::kTransfer;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &host;
        vkCmdPipelineBarrier2(command, &dependency);
    }

    void saveCapture(const gt4::vk::Image& target) {
        const auto* pixels = static_cast<const uint8_t*>(readback.mapped);
        const size_t count = static_cast<size_t>(target.extent.width) * target.extent.height;
        std::vector<unsigned char> rgb(count * 3);
        const bool bgra = target.format == VK_FORMAT_B8G8R8A8_UNORM || target.format == VK_FORMAT_B8G8R8A8_SRGB;
        const bool a2b10 = target.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        const bool a2r10 = target.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        for (size_t i = 0; i < count; ++i) {
            if (a2b10 || a2r10) {
                uint32_t v = 0;
                std::memcpy(&v, pixels + i * 4, 4);
                const uint32_t low = v & 1023u, mid = (v >> 10) & 1023u, high = (v >> 20) & 1023u;
                const uint32_t r = a2b10 ? low : high, b = a2b10 ? high : low;
                rgb[i * 3] = static_cast<unsigned char>((r * 255u + 511u) / 1023u);
                rgb[i * 3 + 1] = static_cast<unsigned char>((mid * 255u + 511u) / 1023u);
                rgb[i * 3 + 2] = static_cast<unsigned char>((b * 255u + 511u) / 1023u);
            } else {
                rgb[i * 3] = pixels[i * 4 + (bgra ? 2 : 0)];
                rgb[i * 3 + 1] = pixels[i * 4 + 1];
                rgb[i * 3 + 2] = pixels[i * 4 + (bgra ? 0 : 2)];
            }
        }
        std::ofstream output(capturePath, std::ios::binary);
        output << "P6\n" << target.extent.width << " " << target.extent.height << "\n255\n";
        output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!output) throw std::runtime_error("Could not write framebuffer capture: " + capturePath);
        capturePath.clear();
        gt4::vk::destroy(device, readback);
    }

    void readTimestamps(Frame& frame, uint32_t frameSlot) {
        if (!timestampsSupported || !frame.timed) return;
        uint64_t values[2] = {};
        if (vkGetQueryPoolResults(device.device, timestamps, frameSlot * 2, 2, sizeof(values), values, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && values[1] >= values[0])
        {
            gpuMs = static_cast<double>(values[1] - values[0]) * static_cast<double>(device.properties.limits.timestampPeriod) * 1e-6;
            gpuMsTotal += gpuMs;
            ++gpuSamples;
        }
    }

    bool draw(float deltaSeconds) {
        if (!headless && (suspended || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))) { resized = true; return false; }
        if (displayMoved) checkPresentation();
        if (!headless && (presentedVsync != settings.vsync || presentedBits != settings.outputBits || wantedHdr != settings.hdr)) resized = true;
        if (headless && presentedBits != settings.outputBits) resized = true;
        if (resized && !recreateTarget()) return false;
        Frame& frame = frames[slot];
        check(vkWaitForFences(device.device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        readTimestamps(frame, slot);
        uint32_t index = 0;
        bool suboptimal = false;
        if (!headless) {
            VkResult result = vkAcquireNextImageKHR(device.device, swapchain, 100000000, frame.acquired, VK_NULL_HANDLE, &index);
            if (result == VK_TIMEOUT || result == VK_NOT_READY) return false;
            if (result == VK_ERROR_OUT_OF_DATE_KHR) { resized = true; return false; }
            if (result == VK_ERROR_SURFACE_LOST_KHR) { recreateSurface(); resized = true; return false; }
            suboptimal = result == VK_SUBOPTIMAL_KHR;
            if (!suboptimal) check(result, "vkAcquireNextImageKHR");
        }
        gt4::vk::Image& target = headless ? offscreen : swapImages[index];
        if (!headless) {
            // A swapchain image's contents are not kept between frames.
            target.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            target.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            target.access = VK_ACCESS_2_NONE;
        }
        gt4::SceneSource& active = gameFrame ? static_cast<gt4::SceneSource&>(*gsnScene) : static_cast<gt4::SceneSource&>(*scene);
        if (sceneSwitched) { vkDeviceWaitIdle(device.device); pipeline.reset(); pipeline = std::make_unique<gt4::FramePipeline>(device, pool);
                             pipeline->prepareOutput(format, srgbFormat, static_cast<uint32_t>(presentedBits)); sceneSwitched = false; }
        pipeline->configure(settings, extent, active);
        // HDR10 output when the swapchain is (display.hdr on a display that offers it).
        pipeline->setHdr(presentedHdr ? settings.hdrPaperWhite : 0.0f, settings.hdrPeak);
        overlay->setHdr(presentedHdr ? settings.hdrPaperWhite : 0.0f);
        if (gameFrame) {
            gsnScene->setAnisotropy(settings.anisotropy);
            // render.gs = original: the PS2's own resolution (one render pixel per GS pixel); native: the
            // nearest whole scale to the render resolution.
            gsnScene->setScale(settings.gsRenderer == gt4::GsRenderer::Original ? 1 : 0);
            // Lighting (docs/LIGHTING.md): occlusion strength and directions by level, the enhanced grade.
            float occlusion = 0.0f;
            uint32_t directions = 6;
            switch (settings.ambientOcclusion) {
            case gt4::EffectQuality::Low: occlusion = 0.5f; directions = 4; break;
            case gt4::EffectQuality::Medium: occlusion = 0.7f; directions = 6; break;
            case gt4::EffectQuality::High: occlusion = 0.85f; directions = 8; break;
            case gt4::EffectQuality::Ultra: occlusion = 1.0f; directions = 12; break;
            default: break;
            }
            float aspect = gt4::aspectValue(settings.aspect);
            if (aspect <= 0.0f) aspect = extent.height ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 4.0f / 3.0f;
            gsnScene->setLighting(occlusion, directions, settings.lighting == gt4::LightingMode::Enhanced ? 1.0f : 0.0f, aspect);
            // Temporal anti-aliasing in the game's 3D scenes (motion vectors from its 3D renderers): for TAA, and for
            // FSR 2, whose temporal part it takes (the game's frames reach the pipeline whole, HUD included).
            gsnScene->setTemporal(settings.aa == gt4::AntiAliasing::Taa || settings.upscaler == gt4::Upscaler::Fsr2);
            // The scene stage's effects, by their settings' levels (original and off: none).
            auto level = [](gt4::EffectQuality q) {
                switch (q) {
                case gt4::EffectQuality::Low: return 1;
                case gt4::EffectQuality::Medium: return 2;
                case gt4::EffectQuality::High: return 3;
                case gt4::EffectQuality::Ultra: return 4;
                default: return 0;
                }
            };
            gt4::SceneEffects fx;
            if (const int q = level(settings.reflections)) {
                fx.reflections = 0.4f + 0.15f * float(q);
                fx.reflectionSteps = 12u + 8u * static_cast<uint32_t>(q);
            }
            if (const int q = level(settings.skyFog)) fx.fogDensity = 0.0002f + 0.0001f * float(q);
            if (const int q = level(settings.motionBlur)) {
                fx.shutter = settings.shutter;
                fx.blurSamples = 4u + 4u * static_cast<uint32_t>(q);
            }
            if (const int q = level(settings.dof)) {
                fx.aperture = settings.aperture;
                fx.focus = settings.focusDistance;
                fx.dofSamples = 16u * static_cast<uint32_t>(q);
            }
            gsnScene->setEffects(fx);
        }
        scene->setControls(stick, action);
        scene->apply(settings);
        const gt4::SceneView view = pipeline->beginFrame(slot, frameCounter);
        composeOverlay(slot);
        check(vkResetCommandBuffer(frame.command, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");
        if (timestampsSupported) {
            vkCmdResetQueryPool(frame.command, timestamps, slot * 2, 2);
            vkCmdWriteTimestamp2(frame.command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timestamps, slot * 2);
        }
        pipeline->record(frame.command, active, view, target, *overlay, deltaSeconds);
        const bool capturing = !capturePath.empty();
        if (capturing) recordCapture(frame.command, target);
        if (!headless) {
            gt4::vk::Barriers b;
            b.image(target, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE);
            b.flush(frame.command);
        }
        if (timestampsSupported) vkCmdWriteTimestamp2(frame.command, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, timestamps, slot * 2 + 1);
        frame.timed = timestampsSupported;
        check(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (!headless) {
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &frame.acquired;
            submit.pWaitDstStageMask = &stage;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &finished[index];
        }
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.command;
        check(vkResetFences(device.device, 1, &frame.fence), "vkResetFences");
        check(vkQueueSubmit(device.queue, 1, &submit, frame.fence), "vkQueueSubmit");
        if (capturing) {
            check(vkWaitForFences(device.device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(capture)");
            saveCapture(target);
        }
        ++frameCounter;
        slot = (slot + 1) % kFramesInFlight;
        if (headless) return true;
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &finished[index];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &index;
        const VkResult result = vkQueuePresentKHR(device.queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal) resized = true;
        if (result == VK_ERROR_SURFACE_LOST_KHR) { recreateSurface(); resized = true; return true; }
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) check(result, "vkQueuePresentKHR");
        return true;
    }
};

Renderer::Renderer(SDL_Window* window, VkInstance instance, const std::string& gpuRequest, const gt4::Settings& settings)
    : impl(std::make_unique<Impl>()) {
    impl->window = window;
    impl->instance = instance;
    impl->settings = settings;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &impl->surface))
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
    impl->createDevice(gpuRequest);
}

Renderer::Renderer(VkInstance instance, VkExtent2D size, const std::string& gpuRequest, const gt4::Settings& settings)
    : impl(std::make_unique<Impl>()) {
    impl->instance = instance;
    impl->headless = true;
    impl->headlessSize = size;
    impl->settings = settings;
    impl->createDevice(gpuRequest);
}

Renderer::~Renderer() = default;
void Renderer::resize() { impl->resized = true; }
void Renderer::displayChanged() { if (!impl->headless) impl->displayMoved = true; }
bool Renderer::headless() const { return impl->headless; }
void Renderer::resizeHeadless(VkExtent2D size) { impl->headlessSize = size; impl->resized = true; }
void Renderer::suspend(bool paused) { impl->suspended = paused; impl->resized = true; }
void Renderer::apply(const gt4::Settings& settings) { impl->settings = settings; }
bool Renderer::draw(float deltaSeconds) {
    GT4_CPU_SCOPE("render: frame");
    return impl->draw(deltaSeconds);
}
VkExtent2D Renderer::drawableExtent() const { return impl->extent; }
void Renderer::setControls(float horizontal, bool action) {
    impl->stick = std::clamp(horizontal, -1.0f, 1.0f);
    impl->action = action;
}
void Renderer::capture(const std::string& path) {
    if (!impl->capturePath.empty()) throw std::runtime_error("A framebuffer capture is already pending");
    impl->capturePath = path;
}
void Renderer::setStatus(const std::string& text) { impl->status = text; }
void Renderer::setMenu(const gt4::MenuView* view) {
    if (view) impl->menu = *view;
    else impl->menu.reset();
}
void Renderer::showMessage(const std::string& text, float seconds) {
    impl->message = text;
    impl->messageUntil = SDL_GetTicks() + static_cast<Uint64>(seconds * 1000.0f);
}
gt4::TestScene& Renderer::scene() { return *impl->scene; }
void Renderer::useGameFrame(bool enabled) {
    if (impl->gameFrame != enabled) impl->sceneSwitched = true;   // the pipeline prepares the new scene source
    impl->gameFrame = enabled;
}
void Renderer::addNativeFrames(std::vector<gt4::gsn::FrameList>& lists) { impl->gsnScene->addFrames(lists); }
bool Renderer::readGsTarget(const gt4::gsn::ReadRequest& request, std::vector<uint8_t>& out) {
    return impl->gsnScene->read(request, out);
}
const GpuInfo& Renderer::gpu() const { return impl->info; }
double Renderer::gpuMilliseconds() const { return impl->gpuMs; }
double Renderer::averageGpuMilliseconds() const { return impl->gpuSamples ? impl->gpuMsTotal / static_cast<double>(impl->gpuSamples) : 0.0; }
std::string Renderer::pipelineDescription() const { return impl->pipeline->describe(); }
