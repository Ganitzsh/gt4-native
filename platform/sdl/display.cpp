#include "display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace gt4 {
namespace {

DisplayModeInfo info(const SDL_DisplayMode& mode) {
    return {mode.w, mode.h, mode.refresh_rate, mode.pixel_density};
}

std::vector<SDL_DisplayID> orderedDisplays() {
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    std::vector<SDL_DisplayID> result;
    if (!ids) return result;
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (primary) result.push_back(primary);
    for (int i = 0; i < count; ++i)
        if (ids[i] != primary) result.push_back(ids[i]);
    SDL_free(ids);
    return result;
}

SDL_DisplayID displayFor(int index) {
    const auto ids = orderedDisplays();
    if (ids.empty()) return 0;
    if (index < 0 || static_cast<size_t>(index) >= ids.size()) {
        std::fprintf(stderr, "display.monitor %d does not exist (%zu displays); using the primary display\n", index, ids.size());
        return ids.front();
    }
    return ids[static_cast<size_t>(index)];
}

std::string rate(float hz) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f Hz", static_cast<double>(hz));
    return text;
}

}  // namespace

std::vector<DisplayInfo> listDisplays() {
    std::vector<DisplayInfo> result;
    int index = 0;
    for (SDL_DisplayID id : orderedDisplays()) {
        DisplayInfo d;
        d.id = id;
        d.index = index++;
        const char* name = SDL_GetDisplayName(id);
        d.name = name ? name : "display";
        SDL_GetDisplayBounds(id, &d.bounds);
        if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(id)) d.desktop = info(*desktop);
        d.contentScale = SDL_GetDisplayContentScale(id);
        int count = 0;
        if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &count)) {
            for (int i = 0; i < count; ++i) d.modes.push_back(info(*modes[i]));
            SDL_free(modes);
        }
        result.push_back(std::move(d));
    }
    return result;
}

std::string describeDisplays() {
    std::ostringstream out;
    for (const auto& d : listDisplays()) {
        out << d.index << ": " << d.name << (d.index == 0 ? " (primary)" : "") << ", desktop " << d.desktop.width << "x"
            << d.desktop.height << " @ " << rate(d.desktop.refreshRate) << ", at " << d.bounds.x << "," << d.bounds.y
            << ", scale " << d.contentScale << "\n";
        std::string last;
        for (const auto& m : d.modes) {
            std::ostringstream mode;
            mode << "    " << m.width << "x" << m.height << " @ " << rate(m.refreshRate);
            if (m.pixelDensity != 1.0f) mode << " (density " << m.pixelDensity << ")";
            if (mode.str() != last) out << mode.str() << "\n";
            last = mode.str();
        }
    }
    return out.str();
}

std::string windowDisplayName(SDL_Window* window) {
    const SDL_DisplayID id = window ? SDL_GetDisplayForWindow(window) : 0;
    const char* name = id ? SDL_GetDisplayName(id) : nullptr;
    if (!name) return "unknown display";
    SDL_Rect bounds{};
    SDL_GetDisplayBounds(id, &bounds);
    std::ostringstream out;
    out << "\"" << name << "\" (" << bounds.w << "x" << bounds.h << " at " << bounds.x << "," << bounds.y << ")";
    return out.str();
}

void DisplayManager::toggleFullscreen(Settings& settings) {
    settings.windowMode = settings.windowMode == WindowMode::Windowed ? WindowMode::Borderless : WindowMode::Windowed;
}

bool DisplayManager::apply(const Settings& s) {
    const bool monitorChanged = s.monitor != monitor;
    if (applied && s.windowMode == mode && !monitorChanged && s.windowWidth == windowWidth && s.windowHeight == windowHeight &&
        s.fullscreenWidth == fullscreenWidth && s.fullscreenHeight == fullscreenHeight && s.refreshRate == refreshRate)
        return false;
    const SDL_DisplayID display = displayFor(s.monitor);
    std::ostringstream out;
    bool ok = true;
    if (s.windowMode == WindowMode::Windowed) {
        ok &= SDL_SetWindowFullscreen(window, false);
        ok &= SDL_SetWindowBordered(window, true);
        ok &= SDL_SetWindowSize(window, s.windowWidth, s.windowHeight);
        if (!applied || monitorChanged || mode != WindowMode::Windowed)
            ok &= SDL_SetWindowPosition(window, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
                                        static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
        out << "windowed " << s.windowWidth << "x" << s.windowHeight;
    } else {
        // The window must be on the target display before going fullscreen there.
        if (!applied || monitorChanged || SDL_GetDisplayForWindow(window) != display) {
            ok &= SDL_SetWindowFullscreen(window, false);
            ok &= SDL_SetWindowPosition(window, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
                                        static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
            SDL_SyncWindow(window);
        }
        if (s.windowMode == WindowMode::Borderless) {
            // A null mode is SDL's "fullscreen desktop": a borderless window covering the display, no mode switch.
            ok &= SDL_SetWindowFullscreenMode(window, nullptr);
            out << "borderless fullscreen";
        } else {
            const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
            const int width = s.fullscreenWidth > 0 ? s.fullscreenWidth : (desktop ? desktop->w : 0);
            const int height = s.fullscreenHeight > 0 ? s.fullscreenHeight : (desktop ? desktop->h : 0);
            // The mode with this size and the requested refresh rate (0: the highest rate at this size).
            int count = 0;
            SDL_DisplayMode chosen{};
            bool found = false;
            if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count)) {
                float best = -1.0f;
                for (int i = 0; i < count; ++i) {
                    const SDL_DisplayMode& m = *modes[i];
                    if (m.w != width || m.h != height) continue;
                    const float score = s.refreshRate > 0.0f ? -std::fabs(m.refresh_rate - s.refreshRate) : m.refresh_rate;
                    if (!found || score > best) { chosen = m; best = score; found = true; }
                }
                SDL_free(modes);
            }
            if (!found && SDL_GetClosestFullscreenDisplayMode(display, width, height, s.refreshRate, true, &chosen)) found = true;
            if (found) {
                ok &= SDL_SetWindowFullscreenMode(window, &chosen);
                out << "exclusive fullscreen " << chosen.w << "x" << chosen.h << " @ " << rate(chosen.refresh_rate);
            } else {
                std::fprintf(stderr, "No exclusive mode %dx%d on display %d; using borderless fullscreen\n", width, height, s.monitor);
                ok &= SDL_SetWindowFullscreenMode(window, nullptr);
                out << "borderless fullscreen (no exclusive mode " << width << "x" << height << ")";
            }
        }
        ok &= SDL_SetWindowFullscreen(window, true);
    }
    SDL_SyncWindow(window);
    if (!ok) std::fprintf(stderr, "Display mode change incomplete: %s\n", SDL_GetError());
    out << " on " << windowDisplayName(window);
    state = out.str();
    std::printf("Display: %s\n", state.c_str());
    applied = true;
    mode = s.windowMode;
    monitor = s.monitor;
    windowWidth = s.windowWidth;
    windowHeight = s.windowHeight;
    fullscreenWidth = s.fullscreenWidth;
    fullscreenHeight = s.fullscreenHeight;
    refreshRate = s.refreshRate;
    return true;
}

std::string DisplayManager::describe() const { return state; }

}  // namespace gt4
