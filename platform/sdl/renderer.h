#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

struct SDL_Window;

// Owns only host rendering resources. The PS2 graphics translation belongs above this layer.
class Renderer {
public:
    Renderer(SDL_Window* window, VkInstance instance, VkSurfaceKHR surface,
             VkPhysicalDevice physical, uint32_t family, PFN_vkGetInstanceProcAddr get);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize();
    void suspend(bool paused);
    // Returns false when minimized, out of date, or waiting for a drawable window.
    bool draw();
    VkExtent2D drawableExtent() const;
    void setControls(float horizontal, bool action);
    void capture(const std::string& path);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
