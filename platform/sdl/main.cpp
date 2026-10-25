#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include "renderer.h"
#include "session.h"
#include <charconv>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
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

// Resource ownership also covers partially initialized startup failures.
struct Host {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkGetInstanceProcAddr get = nullptr;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    ~Host() {
        if (surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        if (instance && destroyInstance) destroyInstance(instance, nullptr);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
    void start(bool hidden) {
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL_Init");
        window = SDL_CreateWindow("GT4 native host - engine integration pending", 1280, 720,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                  (hidden ? SDL_WINDOW_HIDDEN : 0));
        require(window != nullptr, "SDL_CreateWindow");
        get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        require(get != nullptr, "SDL_Vulkan_GetVkGetInstanceProcAddr");
        const auto create = load<PFN_vkCreateInstance>(get, VK_NULL_HANDLE, "vkCreateInstance");
        Uint32 extensionCount = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        require(extensions != nullptr, "SDL_Vulkan_GetInstanceExtensions");
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "GT4 native host";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = extensionCount;
        info.ppEnabledExtensionNames = extensions;
        check(create(&info, nullptr, &instance), "vkCreateInstance");
        destroyInstance = load<PFN_vkDestroyInstance>(get, instance, "vkDestroyInstance");
        require(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface), "SDL_Vulkan_CreateSurface");
        inspectDevices(get);
    }
    void inspectDevices(PFN_vkGetInstanceProcAddr loader) {
        auto enumerate = load<PFN_vkEnumeratePhysicalDevices>(loader, instance, "vkEnumeratePhysicalDevices");
        auto properties = load<PFN_vkGetPhysicalDeviceProperties>(loader, instance, "vkGetPhysicalDeviceProperties");
        auto queues = load<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(loader, instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        auto support = load<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(loader, instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        auto deviceExtensions = load<PFN_vkEnumerateDeviceExtensionProperties>(loader, instance, "vkEnumerateDeviceExtensionProperties");
        uint32_t count = 0;
        check(enumerate(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> devices(count);
        if (count) check(enumerate(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        devices.resize(count);
        const auto score = [&](VkPhysicalDevice device) {
            VkPhysicalDeviceProperties details{};
            properties(device, &details);
            return details.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
                   details.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        };
        std::stable_sort(devices.begin(), devices.end(), [&](auto a, auto b) { return score(a) > score(b); });
        for (auto device : devices) {
            uint32_t extensionsCount = 0;
            check(deviceExtensions(device, nullptr, &extensionsCount, nullptr), "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> extensions(extensionsCount);
            check(deviceExtensions(device, nullptr, &extensionsCount, extensions.data()), "vkEnumerateDeviceExtensionProperties");
            bool swapchain = false;
            for (uint32_t i = 0; i < extensionsCount; ++i)
                swapchain |= std::strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            if (!swapchain) continue;
            uint32_t queueCount = 0;
            queues(device, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(queueCount);
            queues(device, &queueCount, families.data());
            for (uint32_t i = 0; i < queueCount; ++i) {
                VkBool32 present = VK_FALSE;
                check(support(device, i, surface, &present), "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (present && families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    physical = device;
                    queueFamily = i;
                    VkPhysicalDeviceProperties details{};
                    properties(device, &details);
                    std::printf("Vulkan startup OK: %s (graphics/present queue %u)\n", details.deviceName, i);
                    return;
                }
            }
        }
        throw std::runtime_error("No Vulkan device with a graphics/present queue and VK_KHR_swapchain");
    }
};
}

int main(int argc, char** argv) {
    bool probe = false, smoke = false;
    uint32_t frameLimit = 0;
    std::string sessionDirectory;
    bool sessionAudio = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: gt4_host [--probe | --frames N | --smoke-test] [--session-dir PATH] [--no-audio]\n"
                      "SDL3/Vulkan host; does not run GT4 yet.\n"
                      "--probe: verify a window surface and compatible GPU, then exit.\n"
                      "--frames N: present N diagnostic frames, then exit.\n"
                      "--smoke-test: present 180 frames with resize/minimize/restore checks.\n"
                      "--session-dir PATH: attach a virtual gamepad, process commands, record frames/audio/events.\n"
                      "--no-audio: disable playback and WAV capture for a session.\n"
                      "Without options: show the diagnostic pattern; Escape closes it.");
            return 0;
        }
        if (std::strcmp(argv[i], "--probe") == 0) probe = true;
        else if (std::strcmp(argv[i], "--smoke-test") == 0) smoke = true;
        else if (std::strcmp(argv[i], "--session-dir") == 0 && i + 1 < argc) sessionDirectory = argv[++i];
        else if (std::strcmp(argv[i], "--no-audio") == 0) sessionAudio = false;
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            const char* value = argv[++i];
            const char* end = value + std::strlen(value);
            const auto parsed = std::from_chars(value, end, frameLimit);
            if (parsed.ec != std::errc{} || parsed.ptr != end || frameLimit == 0) {
                std::fputs("--frames requires a positive integer\n", stderr); return 2;
            }
        } else { std::fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]); return 2; }
    }
    if ((probe && (smoke || frameLimit || !sessionDirectory.empty())) || (smoke && frameLimit)) {
        std::fputs("Choose only one of --probe, --frames, and --smoke-test\n", stderr); return 2;
    }
    if (smoke) frameLimit = 180;
    try {
        Host host;
        host.start(probe);
        if (probe) return 0;
        Renderer renderer(host.window, host.instance, host.surface, host.physical, host.queueFamily, host.get);
        std::unique_ptr<Session> session;
        if (!sessionDirectory.empty()) session = std::make_unique<Session>(sessionDirectory, sessionAudio, true);
        bool running = true, firstResize = false, minimized = false, restored = false, sawMinimized = false;
        uint32_t frames = 0;
        const Uint64 started = SDL_GetTicks();
        Uint64 restoreAt = 0;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) running = false;
                if (event.type == SDL_EVENT_WINDOW_MINIMIZED) renderer.suspend(true);
                if (event.type == SDL_EVENT_WINDOW_RESTORED) renderer.suspend(false);
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESTORED)
                    renderer.resize();
            }
            if (session) {
                running &= session->tick(renderer, host.window, frames);
                renderer.setControls(session->horizontal(), session->action());
            } else {
                // Diagnostic animation when no scripted controller owns the scene.
                renderer.setControls(static_cast<float>(std::sin(SDL_GetTicks() / 900.0)), false);
            }
            if (!running) break;
            if (smoke && SDL_GetTicks() - started > 20000)
                throw std::runtime_error("Rendering smoke test timed out");
            if (smoke && frames >= 30 && !firstResize) {
                require(SDL_SetWindowSize(host.window, 800, 450), "SDL_SetWindowSize");
                renderer.resize();
                firstResize = true;
            }
            const auto drawable = renderer.drawableExtent();
            if (smoke && frames >= 60 && drawable.width == 800 && drawable.height == 450 && !minimized) {
                require(SDL_MinimizeWindow(host.window), "SDL_MinimizeWindow");
                restoreAt = SDL_GetTicks() + 200;
                minimized = true;
                renderer.suspend(true);
            }
            if (minimized && !restored && SDL_GetTicks() >= restoreAt) {
                require(SDL_RestoreWindow(host.window), "SDL_RestoreWindow");
                require(SDL_SetWindowSize(host.window, 1280, 720), "SDL_SetWindowSize");
                renderer.resize();
                restored = true;
                renderer.suspend(false);
            }
            // Some desktop compositors acknowledge minimize asynchronously. Keep the
            // finite smoke test paused until its restore request, even if frames are fast.
            if (minimized && !restored) {
                sawMinimized = true;
                if (renderer.draw()) throw std::runtime_error("Renderer presented while suspended");
                SDL_Delay(10); continue;
            }
            const Uint64 frameStarted = SDL_GetPerformanceCounter();
            if (renderer.draw()) {
                ++frames;
                if (session) session->frame(frames, static_cast<double>(SDL_GetPerformanceCounter() - frameStarted) *
                                           1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()),
                                           renderer.drawableExtent().width, renderer.drawableExtent().height);
                const auto size = renderer.drawableExtent();
                if (frameLimit && frames >= frameLimit && (!smoke || (restored && sawMinimized && size.width == 1280 && size.height == 720))) running = false;
                if (smoke || session) SDL_Delay(8);
            } else SDL_Delay(10);
        }
        if (frameLimit && frames < frameLimit)
            throw std::runtime_error("Window closed before the requested frames were presented");
        if (smoke && (!firstResize || !minimized || !restored || !sawMinimized))
            throw std::runtime_error("Rendering smoke test did not complete window transitions");
        std::printf("Presented %u frames%s\n", frames, smoke ? "; resize/minimize/restore smoke test OK" : "");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "GT4 native failed: %s\n", error.what());
        return 1;
    }
}
