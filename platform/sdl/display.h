#pragma once
// Window and display-mode management for gt4_host: the monitor list, windowed / borderless fullscreen /
// exclusive fullscreen (with resolution and refresh-rate choice), and moving the window to a monitor.
// Rendering adapts to whatever pixel size the window ends up with (Renderer::resize); nothing here touches
// game state.
#include "settings.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace gt4 {

struct DisplayModeInfo {
    int width = 0, height = 0;
    float refreshRate = 0.0f;
    float pixelDensity = 1.0f;
};

struct DisplayInfo {
    SDL_DisplayID id = 0;
    int index = 0;                        // display.monitor value: 0 is the primary display
    std::string name;
    SDL_Rect bounds{};                    // desktop coordinates (points)
    DisplayModeInfo desktop;
    float contentScale = 1.0f;            // the OS scale factor (Windows per-monitor DPI / 96)
    std::vector<DisplayModeInfo> modes;   // exclusive fullscreen modes, largest first
};

// Connected displays, the primary one first. Requires SDL's video subsystem.
std::vector<DisplayInfo> listDisplays();
std::string describeDisplays();
// Name of the display the window is on ("unknown display" when SDL cannot tell).
std::string windowDisplayName(SDL_Window* window);

class DisplayManager {
public:
    explicit DisplayManager(SDL_Window* window) : window(window) {}
    // Applies the [display] window options when they differ from what was applied last. Returns false
    // when nothing changed. Logs what it did; errors are reported and leave the previous mode in place.
    bool apply(const Settings& settings);
    // One-line description of the current window state for the on-screen readout.
    std::string describe() const;
    // Toggles between windowed and the last fullscreen mode (Alt+Enter, F11).
    static void toggleFullscreen(Settings& settings);

private:
    SDL_Window* window;
    bool applied = false;
    WindowMode mode = WindowMode::Windowed;
    int monitor = -1, windowWidth = 0, windowHeight = 0, fullscreenWidth = -1, fullscreenHeight = -1;
    float refreshRate = -1.0f;
    std::string state;
};

}  // namespace gt4
