#pragma once
// The host renderer: Vulkan device, presentation (a swapchain, or an offscreen image in headless mode),
// frames in flight, captures, and the display pipeline (platform/render/pipeline.h) fed by the test scene.
// The PS2 graphics translation (R1) will replace the test scene behind the same SceneSource interface.
#include "settings.h"
#include "settings_menu.h"

#include <volk.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct SDL_Window;

namespace gt4 { class TestScene; class GsScene; }

struct GpuInfo {
    uint32_t index = 0;
    std::string name, type, driver, api;
};

namespace gt4::gsn { struct FrameList; }

class Renderer {
public:
    // Windowed: presents to the window. The GPU must be able to present to the window's display.
    Renderer(SDL_Window* window, VkInstance instance, const std::string& gpuRequest, const gt4::Settings& settings);
    // Headless: renders into an offscreen image of the given size (no window, surface or swapchain).
    Renderer(VkInstance instance, VkExtent2D size, const std::string& gpuRequest, const gt4::Settings& settings);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void resize();                           // window size changed
    void displayChanged();                   // the window moved to another display: re-check presentation
    void resizeHeadless(VkExtent2D size);    // headless output size
    bool headless() const;
    void suspend(bool paused);
    void apply(const gt4::Settings& settings);
    // Renders and presents one frame. Returns false when nothing was presented (minimized, out of date).
    // Throws PresentationLost when the window's display cannot be presented to by this GPU any more.
    bool draw(float deltaSeconds);
    VkExtent2D drawableExtent() const;
    void setControls(float horizontal, bool action);
    void capture(const std::string& path);
    void setStatus(const std::string& text);  // extra lines for the on-screen readout
    // The settings menu drawn over the frame (platform/sdl/settings_menu.h); nullptr hides it.
    void setMenu(const gt4::MenuView* view);
    // A short message at the bottom of the screen (a setting changed by a key), for `seconds`.
    void showMessage(const std::string& text, float seconds = 2.5f);
    gt4::TestScene& scene();
    // The PS2's displayed frame (GS back end) replaces the test scene and its HUD (docs/GS_BACKEND.md).
    void useGameFrame(bool enabled);
    void setGameFrame(uint32_t width, uint32_t height, const uint32_t* rgba);
    // The native renderer's frame list (docs/NATIVE_RENDERER.md), shown instead of the GS back end's frame
    // while useNativeFrames is on and once one has arrived. Swapped (the caller gets an old list's buffers
    // back).
    void setNativeFrame(gt4::gsn::FrameList& list);
    void useNativeFrames(bool enabled);
    // Reloads volk's global Vulkan functions for this renderer's instance and device (after another
    // component, such as the GS back end's own device, loaded its own).
    void reloadVulkanFunctions();
    const GpuInfo& gpu() const;
    double gpuMilliseconds() const;          // GPU time of the last finished frame (0 if unknown)
    double averageGpuMilliseconds() const;   // mean GPU time over every timed frame so far
    std::string pipelineDescription() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Every Vulkan device, whether it can run the host, and why not (gt4_host --list-gpus). With a window, also
// whether each device can present to the display the window is on.
std::vector<std::string> describeGpus(VkInstance instance, SDL_Window* window);

// The window moved to a display that the current GPU cannot present to. The caller may create a new
// Renderer, which chooses a device again.
class PresentationLost : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
