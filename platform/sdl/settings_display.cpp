// The window, the display and the aspect ratio: display.*, aspect.* (settings_table.h).
#include "settings_table.h"

namespace gt4::settings_table {

void displayOptions(std::vector<Option>& o) {
    o.push_back(enumOption("display.mode", &Settings::windowMode, kWindowModes,
        "Window mode: a resizable window, borderless fullscreen at the desktop resolution, or exclusive "
        "fullscreen with its own resolution and refresh rate."));
    o.push_back(stringOption("display.gpu", &Settings::gpu, "auto | <index> | <part of the name>",
        "Vulkan device. auto prefers a discrete GPU among the devices that can present to the window's "
        "display (headless: among all devices). --gpu overrides it; gt4_host --list-gpus lists the devices."));
    o.push_back(intOption("display.monitor", &Settings::monitor, 0, 15,
        "Display used for fullscreen and for placing the window (gt4_host --list-displays)."));
    o.push_back(intOption("display.window_width", &Settings::windowWidth, 320, 16384, "Windowed width in points."));
    o.push_back(intOption("display.window_height", &Settings::windowHeight, 200, 16384, "Windowed height in points."));
    o.push_back(intOption("display.fullscreen_width", &Settings::fullscreenWidth, 0, 16384,
        "Exclusive fullscreen width in pixels; 0 keeps the desktop mode."));
    o.push_back(intOption("display.fullscreen_height", &Settings::fullscreenHeight, 0, 16384,
        "Exclusive fullscreen height in pixels; 0 keeps the desktop mode."));
    o.push_back(floatOption("display.refresh_rate", &Settings::refreshRate, 0.0f, 1000.0f,
        "Exclusive fullscreen refresh rate in Hz; 0 picks the highest rate for the resolution."));
    o.push_back(enumOption("display.vsync", &Settings::vsync, kVSync,
        "Presentation: on = FIFO (no tearing), off = IMMEDIATE (tearing, lowest latency), adaptive = "
        "FIFO_RELAXED (tears only when late), mailbox = MAILBOX (no tearing, newest frame wins). Falls "
        "back to FIFO when the surface lacks the mode."));
    o.push_back(intOption("display.fps_limit", &Settings::fpsLimit, 0, 1000,
        "Frame-rate cap in frames per second; 0 = no cap. The game simulation stays at its own 60 Hz."));
    o.push_back(boolOption("display.frame_interpolation", &Settings::frameInterpolation,
        "Presentation above 60 Hz: the game runs at 60 Hz; on = between two of its frames each display refresh "
        "shows the cars and the scenery moved on by the time passed (interpolated, one game frame behind), off = "
        "each game frame is shown until the next."));
    o.push_back(intOption("display.output_bits", &Settings::outputBits, 8, 10,
        "Swapchain precision: 8 or 10 bits per channel (10 reduces banding where the display supports it).", {8, 10}));
    o.push_back(boolOption("display.high_dpi", &Settings::highDpi,
        "Render at the full pixel density of high-DPI displays (otherwise the OS scales the window)."));
    o.push_back(floatOption("display.hdr_paper_white", &Settings::hdrPaperWhite, 80.0f, 500.0f,
        "HDR: the brightness of the game's white (its menus, the sky) in nits."));
    o.push_back(floatOption("display.hdr_peak", &Settings::hdrPeak, 200.0f, 10000.0f,
        "HDR: the display's peak brightness in nits; the game's highlights (sun, glare, lights at night) expand towards it."));
    o.push_back(boolOption("display.hdr", &Settings::hdr,
        "HDR10 output (PQ, BT.2020) where the display offers it: the game's picture at display.hdr_paper_white, its "
        "highlights expanded towards display.hdr_peak. Off, or a display without HDR: SDR."));
    o.push_back(enumOption("display.field_mode", &Settings::fieldMode, kFieldModes,
        "Interlaced screens (the menus: each field drawn half a line from the other): blend = the two fields half and "
        "half, the PS2's picture without its bob (a faint line below the picture and doubled copies of what moves); "
        "clean = without the line a TV hides, and where something moved between the fields only the newer field."));
    o.push_back(enumOption("aspect.ratio", &Settings::aspect, kAspects,
        "Content aspect ratio. Other window shapes get black bars (letterbox or pillarbox). 3D is Hor+: the "
        "vertical field of view stays fixed and wider ratios see more to the sides."));
    o.push_back(enumOption("aspect.hud", &Settings::hud, kHud,
        "2D layout: anchored = HUD elements keep to the edges of the content area and the menus use GT4's own "
        "widescreen layout, centered = 2D stays in a centred 4:3 area (some menu pages misalign), stretched = "
        "the 4:3 layout is stretched (the PS2 on a widescreen TV)."));
    o.push_back(floatOption("aspect.safe_area", &Settings::safeArea, 0.0f, 0.15f,
        "HUD inset from the content edges, as a fraction of the content height."));
}

}  // namespace gt4::settings_table
