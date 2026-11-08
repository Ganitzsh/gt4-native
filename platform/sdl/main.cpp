// gt4_host: the SDL3 + Vulkan platform host. It runs the display pipeline on a test scene, or on the PS2's
// displayed frame from the GS back end: a live gt4_game.exe (--game) or a capture (--replay)
// (docs/RENDERING.md, docs/GS_BACKEND.md). Windowed, or headless (offscreen, no window or surface).
#include <volk.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include "disc_setup.h"
#include "display.h"
#include "gs_host.h"
#include "cpu_profile.h"
#include "ps2_pad.h"
#include "input_map.h"
#include "pad_devices.h"
#include "chord_guard.h"
#include "renderer.h"
#include "save_import.h"
#include "session.h"
#include "settings.h"
#include "settings_menu.h"
#include "test_scene.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#endif

namespace {

constexpr double kTickSeconds = 1.0 / 60.0;   // the game's simulation rate (docs/PLATFORM.md)

void require(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

const char* kUsage =
    "Usage: gt4_host [mode] [options]\n"
    "SDL3/Vulkan host: the display pipeline on a test scene, or on GT4's frames (--game, --replay).\n"
    "Modes (default: interactive window; Escape opens the settings menu):\n"
    "  --probe              verify a window surface and a GPU that can present to it, then exit\n"
    "  --frames N           render N frames, then exit\n"
    "  --smoke-test         180 frames with resize/minimize/restore checks\n"
    "  --list-gpus          list Vulkan devices and whether they can run the host\n"
    "  --list-displays      list displays and their fullscreen modes\n"
    "  --list-options       list every setting with its value, allowed values and help\n"
    "Options:\n"
    "  --headless           render offscreen (no window, surface or swapchain); default GPU: discrete\n"
    "  --size WxH           headless output size (default: display.window_width x display.window_height)\n"
    "  --gpu INDEX|NAME     Vulkan device (overrides display.gpu)\n"
    "  --set KEY=VALUE      override one setting (repeatable), e.g. --set render.aa=smaa\n"
    "  --preset NAME        shorthand for --set graphics.preset=NAME\n"
    "  --settings PATH      settings file to read (interactive runs also save to it)\n"
    "  --capture PATH       write the last frame to PATH (PPM) before exiting (with --frames)\n"
    "  --capture-every N    also write every Nth frame, numbered (PATH_000600.ppm)\n"
    "  --capture-frames LIST  write these frames (from 1; N, A-B, A-B/STEP or every:N, comma-separated) as\n"
    "                       DIR/frame_000600.ppm (with --frames and --capture-dir DIR); a fixed-step replay draws\n"
    "                       one VBlank a frame, so frame N is the capture's N-th VBlank\n"
    "  --profile-json PATH  one JSON line per presented frame: the time since the last present, the draw's CPU\n"
    "                       time, the GPU time of the last finished frame, frames skipped, the render graph's\n"
    "                       passes (GPU and CPU) and transient memory, the GS back end's GPU stages and the host's\n"
    "                       CPU stages (tools/perf_report.py); measure with\n"
    "                       display.frame_interpolation=off (an interpolated present repeats the last GS frame's)\n"
    "  --session-dir PATH   attach a virtual gamepad, process commands, record frames/audio/events\n"
    "  --no-audio           disable playback and WAV capture for a session\n"
    "  --fixed-step         advance exactly one 60 Hz tick per frame (default for headless and sessions)\n"
    "  --game PATH          start gt4_game.exe with a link and display its frames (GS back end)\n"
    "  --game-args \"ARGS\"  extra arguments for the game (after --link NAME)\n"
    "  --replay FILE        display a capture of the game's graphics stream (gt4_game --capture) instead;\n"
    "                       with --fixed-step (headless; --realtime: not) one VBlank per rendered frame, else 60 Hz\n"
    "  --replay-loop        start the replay again at its end\n"
    "  --import-save PATH   copy Gran Turismo 4's saves from a PS2 memory card image (PCSX2 .ps2, a raw dump),\n"
    "                       a folder memory card or a .psu export into the save folder, then exit\n"
    "  --export-save FILE   write the save folder's saves to a new file, then exit: FILE.psu (the default; .psu is\n"
    "                       added to a name without it) one save (--export-name NAME, default BASCUS-97328GAMEDATA),\n"
    "                       or FILE.ps2 a new PCSX2 memory card with all of Gran Turismo 4's saves. An existing\n"
    "                       FILE is refused (--force: replaced by the new file; an existing card is never opened)\n"
    "  --saves DIR          the save folder (default %LOCALAPPDATA%\\gt4-native\\saves); passed to the game\n"
    "Interactive runs read and save the settings file in the user's preference directory; scripted runs\n"
    "(--frames, --smoke-test, --session-dir, --headless) start from the defaults unless --settings is given.\n"
    "Keys: F1 readout, F2 preset, F3 anti-aliasing, F4 upscaler, F5 render quality, F6 aspect ratio,\n"
    "F7 tonemapper, F8 HUD layout, F9 vsync, F10 MSAA, F11/Alt+Enter fullscreen, B bloom, N depth of field,\n"
    "M motion blur, H shadows, R reflections, F fog, T texture filter, Y anisotropy, [ ] render scale,\n"
    "- = sharpness, G grain, V vignette, C aberration, P pause, arrows/left stick camera, Space action;\n"
    "Shift reverses a cycle.";

// --capture-frames LIST: frame numbers from 1 (the frame drawn N-th; a replay with --fixed-step draws one VBlank a
// frame), comma-separated: N, A-B, A-B/STEP, or every:N.
struct FrameList {
    struct Range { uint32_t first, last, step; };
    std::vector<Range> ranges;
    bool empty() const { return ranges.empty(); }
    bool contains(uint32_t frame) const {
        for (const Range& r : ranges)
            if (frame >= r.first && frame <= r.last && (frame - r.first) % r.step == 0) return true;
        return false;
    }
};

FrameList parseFrameList(const std::string& text) {
    FrameList list;
    auto number = [&](const std::string& s) {
        uint32_t v = 0;
        const auto parsed = std::from_chars(s.data(), s.data() + s.size(), v);
        if (s.empty() || parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size() || v == 0)
            throw std::invalid_argument("--capture-frames: '" + s + "' is not a frame number (from 1)");
        return v;
    };
    size_t at = 0;
    while (at <= text.size()) {
        const size_t comma = std::min(text.find(',', at), text.size());
        std::string item = text.substr(at, comma - at);
        at = comma + 1;
        if (item.rfind("every:", 0) == 0) {
            list.ranges.push_back({number(item.substr(6)), UINT32_MAX, number(item.substr(6))});
            continue;
        }
        uint32_t step = 1;
        if (const auto slash = item.find('/'); slash != std::string::npos) {
            step = number(item.substr(slash + 1));
            item.resize(slash);
        }
        const auto dash = item.find('-');
        const uint32_t first = number(item.substr(0, dash));
        const uint32_t last = dash == std::string::npos ? first : number(item.substr(dash + 1));
        if (last < first) throw std::invalid_argument("--capture-frames: a range ends before it starts: " + item);
        list.ranges.push_back({first, last, step});
    }
    return list;
}

struct Options {
    bool probe = false, smoke = false, headless = false, listGpus = false, listDisplays = false, listOptions = false;
    bool fixedStep = false, audio = true, replayLoop = false, realtime = false, force = false;
    std::string game, replay;
    std::vector<std::string> gameArgs;
    uint32_t frames = 0;
    uint32_t captureEvery = 0;  // --capture-every: also every N frames, numbered
    FrameList captureFrames;    // --capture-frames: the frames written to --capture-dir
    std::string captureDir;
    std::string profileJson;    // --profile-json: one line of timings per presented frame
    VkExtent2D size{0, 0};   // headless output; 0 = the window size setting
    std::string gpu, settingsPath, sessionDirectory, capture, importSave, exportSave, exportName, saves;
    std::vector<std::pair<std::string, std::string>> sets;
};

bool parseUnsigned(const std::string& text, uint32_t& value) {
    const char* begin = text.c_str();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == end;
}

// Splits --game-args on spaces; double quotes group words ("a b" is one argument).
std::vector<std::string> splitArguments(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    bool quoted = false, any = false;
    for (char c : text) {
        if (c == '"') { quoted = !quoted; any = true; continue; }
        if ((c == ' ' || c == '\t') && !quoted) {
            if (any) { out.push_back(current); current.clear(); any = false; }
            continue;
        }
        current += c;
        any = true;
    }
    if (any) out.push_back(current);
    return out;
}

// A host path as an argument for the game. On Linux the game is a Windows program under Wine, where / is Z:\ (the
// link's path is given the same way, link_host.cpp): an absolute path becomes Z:\..., which Wine opens wherever the
// game's working directory is. Elsewhere, and for a relative path, the path as it is.
std::string gamePathArgument(const std::string& path) {
#ifdef _WIN32
    return path;
#else
    if (path.empty() || path[0] != '/') return path;
    std::string wine = "Z:" + path;
    std::replace(wine.begin(), wine.end(), '/', '\\');
    return wine;
#endif
}

Options parse(int argc, char** argv) {
    Options o;
    auto value = [&](int& i) -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument(std::string(argv[i]) + " needs a value");
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { std::puts(kUsage); std::exit(0); }
        else if (a == "--probe") o.probe = true;
        else if (a == "--smoke-test") o.smoke = true;
        else if (a == "--headless") o.headless = true;
        else if (a == "--list-gpus") o.listGpus = true;
        else if (a == "--list-displays") o.listDisplays = true;
        else if (a == "--list-options") o.listOptions = true;
        else if (a == "--fixed-step") o.fixedStep = true;
        else if (a == "--realtime") o.realtime = true;
        else if (a == "--no-audio") o.audio = false;
        else if (a == "--gpu") o.gpu = value(i);
        else if (a == "--settings") o.settingsPath = value(i);
        else if (a == "--session-dir") o.sessionDirectory = value(i);
        else if (a == "--capture") o.capture = value(i);
        else if (a == "--capture-every") {
            if (!parseUnsigned(value(i), o.captureEvery) || o.captureEvery == 0) throw std::invalid_argument("--capture-every requires a positive integer");
        }
        else if (a == "--capture-frames") o.captureFrames = parseFrameList(value(i));
        else if (a == "--capture-dir") o.captureDir = value(i);
        else if (a == "--profile-json") o.profileJson = value(i);
        else if (a == "--game") o.game = value(i);
        else if (a == "--game-args") o.gameArgs = splitArguments(value(i));
        else if (a == "--replay") o.replay = value(i);
        else if (a == "--replay-loop") o.replayLoop = true;
        else if (a == "--import-save") o.importSave = value(i);
        else if (a == "--export-save") o.exportSave = value(i);
        else if (a == "--export-name") o.exportName = value(i);
        else if (a == "--force") o.force = true;
        else if (a == "--saves") o.saves = value(i);
        else if (a == "--preset") o.sets.emplace_back("graphics.preset", value(i));
        else if (a == "--set") {
            const std::string pair = value(i);
            const auto equals = pair.find('=');
            if (equals == std::string::npos) throw std::invalid_argument("--set needs KEY=VALUE");
            o.sets.emplace_back(pair.substr(0, equals), pair.substr(equals + 1));
        } else if (a == "--frames") {
            if (!parseUnsigned(value(i), o.frames) || o.frames == 0) throw std::invalid_argument("--frames requires a positive integer");
        } else if (a == "--size") {
            const std::string text = value(i);
            const auto x = text.find('x');
            uint32_t w = 0, h = 0;
            if (x == std::string::npos || !parseUnsigned(text.substr(0, x), w) || !parseUnsigned(text.substr(x + 1), h) ||
                w < 64 || h < 64 || w > 16384 || h > 16384)
                throw std::invalid_argument("--size needs WIDTHxHEIGHT, 64..16384");
            o.size = {w, h};
        } else throw std::invalid_argument("Unknown or incomplete option: " + a);
    }
    const int modes = int(o.probe) + int(o.smoke) + int(o.frames > 0) + int(o.listGpus) + int(o.listDisplays) + int(o.listOptions);
    if (modes > 1) throw std::invalid_argument("Choose only one of --probe, --frames, --smoke-test and the --list options");
    if (o.probe && !o.sessionDirectory.empty()) throw std::invalid_argument("--probe cannot record a session");
    if (o.headless && (o.smoke || o.probe)) throw std::invalid_argument("--smoke-test and --probe need a window");
    if (!o.game.empty() && !o.replay.empty()) throw std::invalid_argument("Choose one of --game and --replay");
    if (!o.capture.empty() && !o.frames) throw std::invalid_argument("--capture needs --frames");
    if (!o.captureFrames.empty() && o.captureDir.empty()) throw std::invalid_argument("--capture-frames needs --capture-dir");
    if (!o.captureFrames.empty() && !o.frames) throw std::invalid_argument("--capture-frames needs --frames");
    if (o.smoke) o.frames = 180;
    if ((o.headless || !o.sessionDirectory.empty()) && !o.realtime) o.fixedStep = true;
    return o;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage)
        std::fprintf(stderr, "Vulkan %s: %s\n", severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning",
                     data->pMessage);
    return VK_FALSE;
}

// The Vulkan instance: through SDL's loader with a window, through the system loader headless.
struct Instance {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    explicit Instance(bool windowed) {
        if (windowed) {
            const auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
            require(get != nullptr, "SDL_Vulkan_GetVkGetInstanceProcAddr");
            volkInitializeCustom(get);
        } else if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error("The Vulkan loader is not installed (vulkan-1.dll / libvulkan.so.1)");
        }
        if (volkGetInstanceVersion() < VK_API_VERSION_1_3)
            throw std::runtime_error("The Vulkan loader is older than 1.3; update the GPU driver");
        std::vector<const char*> extensions;
        if (windowed) {
            Uint32 count = 0;
            const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
            require(names != nullptr, "SDL_Vulkan_GetInstanceExtensions");
            extensions.assign(names, names + count);
        }
        uint32_t available = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &available, nullptr);
        std::vector<VkExtensionProperties> properties(available);
        vkEnumerateInstanceExtensionProperties(nullptr, &available, properties.data());
        bool debugUtils = false, colourSpaces = false;
        for (const auto& p : properties) {
            debugUtils |= std::strcmp(p.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
            colourSpaces |= std::strcmp(p.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0;
        }
        if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        // HDR10 swapchains (display.hdr): windowed only, with SDL's surface extensions (it needs VK_KHR_surface;
        // headless runs enable none: VUID-vkCreateInstance-ppEnabledExtensionNames-01388).
        if (colourSpaces && windowed) extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "GT4 native host";
        app.pEngineName = "gt4-native";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        std::string names;
        for (const auto& layer : layers) names += std::string(names.empty() ? "" : ", ") + layer.layerName;
        std::printf("Vulkan layers available: %s\n", names.empty() ? "none" : names.c_str());
        VkResult result = vkCreateInstance(&info, nullptr, &instance);
        if (result != VK_SUCCESS && result != VK_ERROR_EXTENSION_NOT_PRESENT && result != VK_ERROR_INCOMPATIBLE_DRIVER) {
            // Implicit layers (overlays of launchers, recorders, remote-play tools) are loaded into every Vulkan
            // application; a broken one makes instance creation fail. Retry without them (Vulkan loader 1.3.234+).
            std::fprintf(stderr, "vkCreateInstance failed (%d); retrying with implicit layers disabled\n", static_cast<int>(result));
            SDL_setenv_unsafe("VK_LOADER_LAYERS_DISABLE", "~implicit~", 1);
            result = vkCreateInstance(&info, nullptr, &instance);
        }
        if (result != VK_SUCCESS) throw std::runtime_error("vkCreateInstance: Vulkan result " + std::to_string(static_cast<int>(result)));
        volkLoadInstanceOnly(instance);
        if (debugUtils && vkCreateDebugUtilsMessengerEXT) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = debugMessage;
            vkCreateDebugUtilsMessengerEXT(instance, &messengerInfo, nullptr, &messenger);
        }
    }
    ~Instance() {
        if (messenger) vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
};

struct SdlRuntime {
    explicit SdlRuntime(SDL_InitFlags flags) { require(SDL_Init(flags), "SDL_Init"); }
    ~SdlRuntime() { SDL_Quit(); }
    SdlRuntime(const SdlRuntime&) = delete;
    SdlRuntime& operator=(const SdlRuntime&) = delete;
};

struct Window {
    SDL_Window* window = nullptr;
    Window(const gt4::Settings& s, bool hidden) {
        SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        if (s.highDpi) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (hidden) flags |= SDL_WINDOW_HIDDEN;
        window = SDL_CreateWindow("GT4 native host - display pipeline test scene", s.windowWidth, s.windowHeight, flags);
        require(window != nullptr, "SDL_CreateWindow");
    }
    ~Window() { if (window) SDL_DestroyWindow(window); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
};

std::string defaultSettingsPath() {
    char* base = SDL_GetPrefPath("gt4-native", "host");
    if (!base) return {};
    std::string path = std::string(base) + "settings.ini";
    SDL_free(base);
    return path;
}

// Moves a setting to the next of its allowed values (enum and choice lists: "a | b | c").
std::string cycle(gt4::Settings& s, const std::string& key, int direction = 1) {
    for (const auto& option : gt4::describe(s)) {
        if (option.key != key) continue;
        std::vector<std::string> values;
        size_t start = 0;
        while (start <= option.allowed.size()) {
            const size_t bar = option.allowed.find(" | ", start);
            values.push_back(option.allowed.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 3;
        }
        if (key == "graphics.preset" || key == "render.quality")
            values.erase(std::remove(values.begin(), values.end(), "custom"), values.end());
        const auto found = std::find(values.begin(), values.end(), option.value);
        const int count = static_cast<int>(values.size());
        int index = found == values.end() ? 0 : static_cast<int>(found - values.begin());
        index = ((index + direction) % count + count) % count;
        gt4::setOption(s, key, values[static_cast<size_t>(index)]);
        return key + " = " + values[static_cast<size_t>(index)];
    }
    return {};
}

std::string adjust(gt4::Settings& s, const std::string& key, float step, float minimum, float maximum) {
    const float current = std::strtof(gt4::getOption(s, key).c_str(), nullptr);
    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(std::clamp(current + step, minimum, maximum)));
    gt4::setOption(s, key, text);
    return key + " = " + gt4::getOption(s, key);
}

// Keyboard toggles of the interactive test scene. Returns a description of the change (empty: none).
// With the game's frames (gameKeys), only the function keys and Alt+Enter: the rest drive the pad.
std::string keyCommand(gt4::Settings& s, const SDL_KeyboardEvent& key, bool gameKeys = false) {
    const bool functionKey = key.key >= SDLK_F1 && key.key <= SDLK_F12;
    if (gameKeys && !functionKey && key.key != SDLK_RETURN) return {};
    const int direction = (key.mod & SDL_KMOD_SHIFT) ? -1 : 1;
    switch (key.key) {
    case SDLK_F1: s.overlayText = !s.overlayText; return std::string("debug.overlay = ") + (s.overlayText ? "on" : "off");
    case SDLK_F2: return cycle(s, "graphics.preset", direction);
    case SDLK_F3: return cycle(s, "render.aa", direction);
    case SDLK_F4: return cycle(s, "render.upscaler", direction);
    case SDLK_F5: return cycle(s, "render.quality", direction);
    case SDLK_F6: return cycle(s, "aspect.ratio", direction);
    case SDLK_F7: return cycle(s, "post.tonemapper", direction);
    case SDLK_F8: return cycle(s, "aspect.hud", direction);
    case SDLK_F9: return cycle(s, "display.vsync", direction);
    case SDLK_F10: return cycle(s, "render.msaa", direction);
    case SDLK_F11: gt4::DisplayManager::toggleFullscreen(s); return std::string("display.mode = ") + gt4::name(s.windowMode);
    case SDLK_RETURN:
        if (!(key.mod & SDL_KMOD_ALT)) return {};
        gt4::DisplayManager::toggleFullscreen(s);
        return std::string("display.mode = ") + gt4::name(s.windowMode);
    case SDLK_B: return cycle(s, "effects.bloom", direction);
    case SDLK_N: return cycle(s, "effects.dof", direction);
    case SDLK_M: return cycle(s, "effects.motion_blur", direction);
    case SDLK_H: return cycle(s, "effects.shadows", direction);
    case SDLK_R: return cycle(s, "effects.reflections", direction);
    case SDLK_F: return cycle(s, "effects.sky_fog", direction);
    case SDLK_T: return cycle(s, "texture.filter", direction);
    case SDLK_Y: return cycle(s, "texture.anisotropy", direction);
    case SDLK_LEFTBRACKET:
    case SDLK_RIGHTBRACKET: {
        const int scale = static_cast<int>(std::lround(100.0f / gt4::upscaleRatio(s)));
        gt4::setOption(s, "render.quality", "custom");
        gt4::setOption(s, "render.scale", std::to_string(std::clamp(scale + (key.key == SDLK_LEFTBRACKET ? -10 : 10), 50, 200)));
        return "render.scale = " + gt4::getOption(s, "render.scale");
    }
    case SDLK_MINUS: return adjust(s, "render.sharpness", -0.1f, 0.0f, 1.0f);
    case SDLK_EQUALS: return adjust(s, "render.sharpness", 0.1f, 0.0f, 1.0f);
    case SDLK_G: return adjust(s, "post.film_grain", s.filmGrain > 0.0f ? -1.0f : 0.25f, 0.0f, 1.0f);
    case SDLK_V: return adjust(s, "post.vignette", s.vignette > 0.0f ? -1.0f : 0.35f, 0.0f, 1.0f);
    case SDLK_C: return adjust(s, "post.chromatic_aberration", s.chromaticAberration > 0.0f ? -1.0f : 0.3f, 0.0f, 1.0f);
    case SDLK_P: s.animate = !s.animate; return std::string("scene.animate = ") + (s.animate ? "on" : "off");
    default: return {};
    }
}

std::string gamepadCommand(gt4::Settings& s, SDL_GamepadButton button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_START: return cycle(s, "graphics.preset");
    case SDL_GAMEPAD_BUTTON_BACK: s.overlayText = !s.overlayText; return std::string("debug.overlay = ") + (s.overlayText ? "on" : "off");
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return cycle(s, "render.aa", -1);
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return cycle(s, "render.aa");
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return cycle(s, "render.quality", -1);
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return cycle(s, "render.quality");
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return cycle(s, "render.upscaler", -1);
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return cycle(s, "render.upscaler");
    case SDL_GAMEPAD_BUTTON_NORTH: return cycle(s, "aspect.ratio");
    default: return {};
    }
}

// The game's pad motors (GT4_REC_RUMBLE, from the stream's thread) for the main loop, per port: bit 31 set once the
// game has sent any, bit 8 the small motor, bits 0-7 the large one.
std::atomic<uint32_t> g_rumble[2];

// GT4_HOST_KEYS=FRAME:KEY,... (unattended checks of the settings menu): presses KEY (escape up down left right
// return backspace pageup pagedown f1) when FRAME frames have been presented. It also enables the menu
// without a window.
struct ScriptedKey { uint32_t frame; SDL_Keycode key; };
std::vector<ScriptedKey> keyScript() {
    static const std::pair<const char*, SDL_Keycode> names[] = {
        {"escape", SDLK_ESCAPE}, {"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT}, {"right", SDLK_RIGHT},
        {"return", SDLK_RETURN}, {"backspace", SDLK_BACKSPACE}, {"pageup", SDLK_PAGEUP}, {"pagedown", SDLK_PAGEDOWN},
        {"f1", SDLK_F1}};
    std::vector<ScriptedKey> out;
    const char* e = std::getenv("GT4_HOST_KEYS");
    for (const char* p = e; p && *p;) {
        unsigned frame = 0;
        char name[16] = "";
        int n = 0;
        if (std::sscanf(p, "%u:%15[a-z0-9]%n", &frame, name, &n) < 2) break;
        p += n;
        for (const auto& [text, key] : names)
            if (!std::strcmp(name, text)) out.push_back({frame, key});
        if (*p == ',') ++p;
    }
    std::sort(out.begin(), out.end(), [](const ScriptedKey& a, const ScriptedKey& b) { return a.frame < b.frame; });
    return out;
}

std::string jsonQuote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) >= 32) out += c;
    }
    return out + "\"";
}

int run(const Options& options) {
    if (!options.exportSave.empty()) {   // the native saves out, for PCSX2 or a PS2 (docs/SAVES.md)
        const std::filesystem::path folder =
            options.saves.empty() ? gt4::saves::defaultSaveFolder() : std::filesystem::u8path(options.saves);
        std::filesystem::path out = std::filesystem::u8path(options.exportSave);
        if (!out.has_extension()) out += ".psu";   // .psu by default: it cannot damage a card
        std::string ext = out.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        gt4::saves::ExportResult r;
        if (ext == ".ps2") r = gt4::saves::exportCard(folder, out, options.force);
        else if (ext == ".psu")
            r = gt4::saves::exportSave(folder, options.exportName.empty() ? "BASCUS-97328GAMEDATA" : options.exportName,
                                       out, options.force);
        else r.message = "--export-save: the file must end in .psu (one save) or .ps2 (a PCSX2 memory card)";
        std::printf("Saves: %s\n", r.message.c_str());
        return r.ok ? 0 : 1;
    }
    if (!options.importSave.empty()) {   // a save from a PS2, into the native save folder (docs/SAVES.md)
        const std::filesystem::path folder =
            options.saves.empty() ? gt4::saves::defaultSaveFolder() : std::filesystem::u8path(options.saves);
        const gt4::saves::ImportResult r = gt4::saves::importSave(std::filesystem::u8path(options.importSave), folder);
        std::printf("Saves: %s\n", r.message.c_str());
        for (const std::string& name : r.imported) std::printf("  %s\n", name.c_str());
        return r.ok ? 0 : 1;
    }
    // Settings: defaults, then the file, then the command line.
    gt4::Settings settings;
    const bool interactive = !options.probe && !options.frames && options.sessionDirectory.empty() && !options.headless &&
                             !options.listGpus && !options.listDisplays && !options.listOptions;
    std::string settingsPath = options.settingsPath;
    if (settingsPath.empty() && interactive) settingsPath = defaultSettingsPath();
    if (!settingsPath.empty()) {
        for (const auto& warning : gt4::loadSettings(settings, settingsPath)) std::fprintf(stderr, "Settings: %s\n", warning.c_str());
        std::printf("Settings: %s\n", settingsPath.c_str());
    }
    // The pipelines compiled in earlier runs (vk::openPipelineCache): pipeline_cache.bin beside the settings;
    // GT4_PIPELINE_CACHE=FILE another file, =0 none. Paths in UTF-8, as settingsPath.
    {
        const char* e = std::getenv("GT4_PIPELINE_CACHE");
        std::string cache;
        if (e && *e) {
            if (std::strcmp(e, "0") != 0) cache = e;
        } else if (!settingsPath.empty()) {
            const std::filesystem::path settingsFile(std::u8string(settingsPath.begin(), settingsPath.end()));
            const std::u8string file = (settingsFile.parent_path() / "pipeline_cache.bin").u8string();
            cache.assign(file.begin(), file.end());
        }
        Renderer::setPipelineCacheFile(cache);
    }
    // The PS2 frame fills a 4:3 TV picture (640 x 448 with non-square pixels); --set aspect.ratio overrides.
    const bool gsMode = !options.game.empty() || !options.replay.empty();
    if (gsMode && settings.aspect == gt4::AspectRatio::Window) gt4::setOption(settings, "aspect.ratio", "4:3");
    // Over the game, the settings readout only on request (F1, or the settings menu).
    if (gsMode) settings.overlayText = false;
    for (const auto& [key, value] : options.sets) {
        const std::string error = gt4::setOption(settings, key, value);
        if (!error.empty()) throw std::invalid_argument("--set " + key + "=" + value + ": " + error);
    }
    if (!options.gpu.empty()) settings.gpu = options.gpu;
    if (options.listOptions) {
        for (const auto& o : gt4::describe(settings)) {
            std::printf("%s = %s\n    %s\n    %s\n", o.key.c_str(), o.value.c_str(), o.allowed.c_str(), o.help.c_str());
            if (!o.unused.empty()) std::printf("    (%s)\n", o.unused.c_str());
        }
        return 0;
    }
    if (interactive && !settingsPath.empty() && !std::filesystem::exists(settingsPath)) gt4::saveSettings(settings, settingsPath);

    const bool windowed = !options.headless;
    SDL_InitFlags flags = SDL_INIT_GAMEPAD;
    if (windowed || options.listDisplays) flags |= SDL_INIT_VIDEO;
    SdlRuntime sdl(flags);
    if (options.listDisplays) { std::fputs(gt4::describeDisplays().c_str(), stdout); return 0; }
    std::unique_ptr<Window> window;
    if (windowed) {
        require(SDL_Vulkan_LoadLibrary(nullptr), "SDL_Vulkan_LoadLibrary");
        window = std::make_unique<Window>(settings, options.probe || options.listGpus);
    }
    Instance instance(windowed);
    SDL_Window* sdlWindow = window ? window->window : nullptr;
    if (options.listGpus) {
        for (const auto& line : describeGpus(instance.instance, sdlWindow)) std::puts(line.c_str());
        return 0;
    }
    std::unique_ptr<gt4::DisplayManager> display;
    if (sdlWindow) {
        display = std::make_unique<gt4::DisplayManager>(sdlWindow);
        if (!options.probe && !options.smoke) display->apply(settings);
    }
    // Headless output: --size, or the size a window would have.
    const VkExtent2D headlessSize = options.size.width ? options.size
        : VkExtent2D{static_cast<uint32_t>(settings.windowWidth), static_cast<uint32_t>(settings.windowHeight)};
    auto makeRenderer = [&](const std::string& gpu) {
        auto made = windowed ? std::make_unique<Renderer>(sdlWindow, instance.instance, gpu, settings)
                             : std::make_unique<Renderer>(instance.instance, headlessSize, gpu, settings);
        // --fixed-step (replays, sessions): frame interpolation by the presents, not the wall clock, so a run gives the
        // same frames every time with display.frame_interpolation on. Live sessions (--session-dir without --realtime)
        // too, on purpose: they are scripted tests that want the same frames, so each present shows the newest game
        // frame there. A player's run (a window, no --session-dir) keeps the display's clock and interpolates.
        made->setFixedClock(options.fixedStep);
        return made;
    };
    std::unique_ptr<Renderer> renderer = makeRenderer(settings.gpu);
    std::string activeGpu = settings.gpu;
    if (options.probe) {
        std::printf("Probe OK: %s can present to %s\n", renderer->gpu().name.c_str(), gt4::windowDisplayName(sdlWindow).c_str());
        return 0;
    }

    // The game's frames: the native renderer's frame lists from a live game or a capture (the GS host's
    // worker thread follows the stream; the renderer draws the lists).
    std::unique_ptr<gt4::gs::GsHost> gs;
    SDL_AudioStream* gameAudio = nullptr;  // destroyed with the process (after the GS host stops)
    const bool lockstep = !options.replay.empty() && options.fixedStep;
    std::vector<gt4::gsn::FrameList> nativeFrames;
    bool gameExited = false;
    if (gsMode) {
        gt4::gs::GsHost::Options gsOptions;
        gsOptions.gamePath = options.game;
        gsOptions.gameArgs = options.gameArgs;
        if (!options.saves.empty()) {   // the save folder (the game's default otherwise)
            gsOptions.gameArgs.push_back("--saves");
            gsOptions.gameArgs.push_back(gamePathArgument(options.saves));
        }
        // A wide content shape switches on the game's own 16:9 mode, generalised to that aspect (the
        // game's --aspect: Hor+ 3D in the anamorphic 640 x 448 frame, which the host presents at the
        // aspect). Read at start-up: changing aspect.ratio later needs a restart of the game.
        // (aspect.ratio = window: the window's own shape)
        const VkExtent2D startExtent = renderer->drawableExtent();
        const float startAspect = settings.aspect == gt4::AspectRatio::Window
                                      ? (startExtent.height ? float(startExtent.width) / float(startExtent.height) : 0.0f)
                                      : gt4::aspectValue(settings.aspect);
        if (const float a = startAspect; !options.game.empty() && a >= 1.5f) {
            gsOptions.gameArgs.push_back("--aspect");
            gsOptions.gameArgs.push_back(std::to_string(a));
            // The menus' 2D: in a centred 4:3 area, or stretched as on the PS2 (aspect.hud = stretched).
            gsOptions.gameArgs.push_back("--menus");
            // Only aspect.hud = centered keeps them in a 4:3 area: GT4's own 16:9 layout (its font squeezed to
            // the frame) lines every page up, while centring misplaces the pages drawn as one full-screen image
            // under their text (the status page, Home).
            gsOptions.gameArgs.push_back(settings.hud == gt4::HudLayout::Centered ? "centered" : "stretched");
        }
        gsOptions.rumble = [](uint32_t port, uint32_t small, uint32_t large) {
            if (port < 2) g_rumble[port] = 0x80000000u | (small ? 0x100u : 0u) | (large & 0xFFu);
        };
        gsOptions.replayPath = options.replay;
        gsOptions.lockstep = lockstep;
        gsOptions.loop = options.replayLoop;
        // A texture pack (texture.pack_path, or the usual places): found once, switched with texture.hd_pack.
        {
            std::string exeDir, prefDir;
            if (const char* base = SDL_GetBasePath()) exeDir = base;
            if (char* pref = SDL_GetPrefPath("gt4-native", "host")) {
                prefDir = pref;
                SDL_free(pref);
            }
            settings.texturePackFound = gt4::gsn::TexturePack::locate(settings.texturePack, exeDir, prefDir);
            gsOptions.texturePack = settings.texturePackFound;
            gsOptions.hdTextures = settings.hdTextures;
            gsOptions.texturePackSynchronous = options.fixedStep;   // (replays and sessions: the same frames every run)
            if (settings.texturePackFound.empty() && settings.texturePack != "auto")
                std::fprintf(stderr, "Texture pack: none in %s\n", settings.texturePack.c_str());
        }
        // The game's sound to the default output (windowed runs: headless ones stay silent; the game
        // writes its mix to a WAV file with GT4_AUDIO_WAV). A session owns its own audio stream.
        if (options.audio && !options.headless && options.sessionDirectory.empty()) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
                gameAudio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
                if (gameAudio) SDL_ResumeAudioStreamDevice(gameAudio);
            }
            if (!gameAudio) std::fprintf(stderr, "No audio output: %s\n", SDL_GetError());
            else
                gsOptions.audio = [stream = gameAudio](uint32_t rate, const int16_t* frames, size_t count) {
                    if (rate != 48000 || !count) return;
                    // keep at most ~150 ms queued: a host that falls behind drops sound, not latency
                    if (SDL_GetAudioStreamQueued(stream) > 48000 * 4 * 15 / 100) return;
                    SDL_PutAudioStreamData(stream, frames, static_cast<int>(count * 4));
                };
        }
        std::string error;
        gs = gt4::gs::GsHost::create(gsOptions, error);
        if (!gs) throw std::runtime_error(error);
        renderer->useGameFrame(true);
        if (window) SDL_SetWindowTitle(window->window, options.game.empty() ? "GT4 native host - replay" : "GT4 native host");
    }

    std::unique_ptr<Session> session;
    if (!options.sessionDirectory.empty()) {
        session = std::make_unique<Session>(options.sessionDirectory, options.audio, true);
        if (gs) session->onGameClock([&gs] { return gs->vblanks(); });
        session->onSetting([&](const std::string& key, const std::string& value) {
            const std::string error = gt4::setOption(settings, key, value);
            if (error.empty()) {
                renderer->apply(settings);
                if (display) display->apply(settings);
            }
            return error;
        });
        const auto& gpu = renderer->gpu();
        session->record("renderer", "\"gpu\":" + jsonQuote(gpu.name) + ",\"gpu_type\":" + jsonQuote(gpu.type) + ",\"driver\":" +
                        jsonQuote(gpu.driver) + ",\"headless\":" + (windowed ? "false" : "true"));
    }
    // The game's two pads: the gamepads, the ports they hold, their profiles (input_profiles.ini beside settings.ini,
    // interactive runs only: other runs have the defaults) and responses (platform/sdl/pad_devices.h). A session takes
    // only its virtual pads.
    std::string profilesPath;
    if (interactive && !settingsPath.empty()) {
        const std::filesystem::path settingsFile(std::u8string(settingsPath.begin(), settingsPath.end()));
        const std::u8string p = (settingsFile.parent_path() / "input_profiles.ini").u8string();
        profilesPath.assign(p.begin(), p.end());
    }
    gt4::PadDevices pads(profilesPath, session != nullptr);
    SDL_JoystickID sessionPads[2] = {};   // a session's virtual pads as the ports last saw them
    bool pad2Sent = false;            // pad 2 sent as connected, so its removal is told to the game
    uint32_t rumbleApplied[2] = {};    // g_rumble as a session last recorded it
    // The players' cars for the haptics (D14): the newest Vehicle record of entrant 0 (pad 1) and 1 (pad 2, split screen;
    // CONTRACTS a.4 Vehicle::flags), from the frames' scene records, and when it came.
    gt4::gfx::scene::Vehicle playerCar[2] = {};
    Uint64 playerCarAt[2] = {};
    int hapticsRecorded[2][4] = {};     // a session's last recorded haptics, in percent
    auto takeCars = [&](const std::vector<gt4::gsn::FrameList>& lists) {
        for (const auto& list : lists) {
            if (!list.scene || !list.scene->complete) continue;
            for (const gt4::gfx::scene::Vehicle& v : list.scene->vehicles)
                if (v.entrant < 2) {
                    playerCar[v.entrant] = v;
                    playerCarAt[v.entrant] = SDL_GetTicks();
                }
        }
    };
    Uint64 padLast = 0;                // when the pads were last read (their response's dt)
    uint8_t padRecorded[2][18] = {};   // each pad's bytes as a session last recorded them
    auto save = [&] {
        if (interactive && !settingsPath.empty()) {
            try { gt4::saveSettings(settings, settingsPath); }
            catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
        }
    };

    // The settings menu (Esc; the pad's Guide button or Select + Start). Over a live game it pauses the game;
    // afterwards the pad reaches the game again once every button is released.
    gt4::SettingsMenu menu(gsMode);
    menu.setStartValues(settings);
    bool padHeldBack = false;
    gt4::ChordGuard chordGuard;   // Select + Start, the menu's chord: neither reaches the game (chord_guard.h)
    SDL_GamepadButton menuHeld = SDL_GAMEPAD_BUTTON_INVALID;
    Uint64 menuRepeatAt = 0;
    int stickHeld = 0;   // the left stick in the menu: -1 up, 1 down
    const std::vector<ScriptedKey> scriptedKeys = keyScript();
    size_t nextScriptedKey = 0;
    const bool menuEnabled = interactive || !scriptedKeys.empty();
    if (interactive && gsMode) renderer->showMessage("Settings: Esc, or Guide / Select + Start on the pad", 6.0f);

    bool running = true, firstResize = false, minimized = false, restored = false, sawMinimized = false;
    auto openMenu = [&](bool open) {
        if (open) menu.open(settings);
        else menu.close();
        if (!open) padHeldBack = true;
        menuHeld = SDL_GAMEPAD_BUTTON_INVALID;
    };
    auto menuInput = [&](gt4::SettingsMenu::Input input) {
        const auto result = menu.input(input, settings);
        if (result.changed) {
            renderer->apply(settings);
            if (display) display->apply(settings);
            save();
        }
        if (result.closed) openMenu(false);
        if (result.quit) running = false;
    };
    // A pad button in the menu; directions repeat while held.
    auto menuButton = [&](SDL_GamepadButton button) {
        using Input = gt4::SettingsMenu::Input;
        switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: menuInput(Input::Up); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: menuInput(Input::Down); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: menuInput(Input::Left); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: menuInput(Input::Right); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: menuInput(Input::Accept); break;
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_START: menuInput(Input::Back); break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: menuInput(Input::PageUp); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: menuInput(Input::PageDown); break;
        default: return;
        }
        const bool direction = button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
                               button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        menuHeld = direction && menu.isOpen() ? button : SDL_GAMEPAD_BUTTON_INVALID;
        menuRepeatAt = SDL_GetTicks() + 350;
    };
    uint32_t frames = 0;
    const Uint64 started = SDL_GetTicks();
    Uint64 restoreAt = 0;
    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;
    Uint64 nextFrame = SDL_GetTicksNS();
    float keyboardStick = 0.0f;
    bool keyboardAction = false;
    struct Perf {
        Uint64 at;
        uint32_t frames;
        uint64_t vblanks;
        double host, game, gpu;
        uint64_t drawn, fresh;
        uint32_t waits, waitMs;
    } perf{};
    uint64_t newestAdded = 0;   // the VBlank of the newest displayed list given to the renderer (drawn at its next draw)
    // The game's asynchronous reads the GPU has finished (the flare pass's), to the game.
    std::vector<gt4::GsnScene::ReadAnswer> readAnswers;
    auto sendReadAnswers = [&] {
        if (!gs) return;
        GT4_STAGE_SCOPE("main: the game's asynchronous reads");
        renderer->takeReadAnswers(readAnswers);
        for (const auto& a : readAnswers) gs->answerRead(a.id, a.qwc, a.pixels);
        readAnswers.clear();
    };
    uint64_t freshShown = 0;   // presents that showed a frame the game drew since the one before
    bool freshPending = false;
    // since the last present (the hitch log): loop passes that found no new frame of the game, draws that presented nothing
    uint32_t passesWithoutFrame = 0, drawsWithoutPresent = 0;
    std::vector<uint64_t> stagesBefore;   // the stages' totals at the last present (cpuprofile::busiest)
    double perfGpu = 0.0;   // GPU milliseconds of the frames presented so far
    // GT4_TRACE_LATENCY=1 (PLAN.md D11, measurement): the input-to-present chain, a "latency:" line a step, in QPC
    // microseconds (the game's lines, from its pad reads, use the same clock): the input events (their SDL time), pad
    // 1's bytes sent when they change, and each present with the newest game frame it shows. tools/latency_report.py
    // joins them. Off without the variable.
    static const bool traceLatency = [] {
        const char* e = std::getenv("GT4_TRACE_LATENCY");
        return e && *e && std::strcmp(e, "0");
    }();
    const auto qpcUs = [&](Uint64 counter) { return static_cast<double>(counter) * 1e6 / frequency; };
    // SDL's event times (SDL_GetTicksNS) to the same clock
    const double ticksToQpcUs = qpcUs(SDL_GetPerformanceCounter()) - static_cast<double>(SDL_GetTicksNS()) * 1e-3;
    uint8_t latencySent[18] = {};
    // The pads to the game, both ports (live games only). onlyChanged: a port's bytes are sent only if they changed
    // since its last send (the samples taken while the loop waits for the game's frame, below).
    uint8_t padSent[2][18] = {};
    const auto sendPads = [&](bool onlyChanged) {
        // Nothing reaches the game while the menu is open, nor afterwards until every button is up
        // (the button that closed the menu would act in the game).
        // the frame's time for the pads' response (its filters and the keys' ramp): a 60 Hz tick under
        // --fixed-step (replays and sessions: the same pad bytes every run), else the wall clock, a tenth of a
        // second at most
        const Uint64 padNow = SDL_GetTicksNS();
        const float padDt = options.fixedStep ? 1.0f / 60.0f
                            : padLast ? std::min(static_cast<float>(padNow - padLast) * 1e-9f, 0.1f) : 0.0f;
        padLast = padNow;
        gt4::ps2::PadInput input = pads.read(0, settings, padDt);
        if (menuEnabled) chordGuard.filter(input, SDL_GetTicks());
        if (padHeldBack) {
            bool held = std::fabs(input.leftX) > 0.3f || std::fabs(input.leftY) > 0.3f;
            for (float pressure : input.button) held |= pressure > 0.0f;
            padHeldBack = held;
        }
        if (menu.isOpen() || padHeldBack) input = gt4::ps2::PadInput{};
        uint8_t pad[18];
        gt4::ps2::encodePad(input, pad);
        if (!onlyChanged || std::memcmp(pad, padSent[0], sizeof pad) != 0) {
            gs->sendPad(0, true, pad);
            std::memcpy(padSent[0], pad, sizeof pad);
        }
        if (traceLatency && std::memcmp(pad, latencySent, sizeof pad) != 0) {
            std::memcpy(latencySent, pad, sizeof pad);
            char hex[37];
            for (int i = 0; i < 18; ++i) std::snprintf(hex + 2 * i, 3, "%02x", pad[i]);
            std::printf("latency: sent %s at %.0f\n", hex, qpcUs(SDL_GetPerformanceCounter()));
            std::fflush(stdout);
        }
        // a session records each pad's bytes when they change (its tests read what the game was sent)
        const auto recordPad = [&](int port, const uint8_t* bytes) {
            if (!session || std::memcmp(bytes, padRecorded[port], 18) == 0) return;
            std::memcpy(padRecorded[port], bytes, 18);
            char hex[37];
            for (int i = 0; i < 18; ++i) std::snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
            session->record("pad", "\"port\":" + std::to_string(port) + ",\"bytes\":\"" + hex + "\"");
        };
        recordPad(0, pad);
        // Pad 2: the gamepad holding port 2 (no keyboard), held back like pad 1. Without one, port 2 is empty:
        // the game sees no controller there, as on a PS2 (runtime/iop.cpp scePad2GetState); told once when it
        // goes. (in a session: its second virtual pad, button2 / axis2)
        if (pads.connected(1)) {
            gt4::ps2::PadInput input2 = pads.read(1, settings, padDt);
            if (menu.isOpen() || padHeldBack) input2 = gt4::ps2::PadInput{};
            gt4::ps2::encodePad(input2, pad);
            if (!onlyChanged || std::memcmp(pad, padSent[1], sizeof pad) != 0) {
                gs->sendPad(1, true, pad);
                std::memcpy(padSent[1], pad, sizeof pad);
            }
            recordPad(1, pad);
            pad2Sent = true;
        } else if (pad2Sent) {
            gt4::ps2::encodePad(gt4::ps2::PadInput{}, pad);
            gs->sendPad(1, false, pad);
            pad2Sent = false;
            if (session) session->record("pad", "\"port\":1,\"connected\":false");
            std::memset(padRecorded[1], 0, 18);   // (a pad plugged in again is recorded again)
        }
    };
    // PLAN.md D11: while the loop waits for the game's next frame (presenting the game's frames, a live game, not
    // --fixed-step), the pads are sampled every millisecond and sent when they change, so the game's next pad read (its
    // VBlank) gets them at most about a millisecond old. Before, they were sampled once a pass, just after the game's
    // frame came, so most of a frame old when the game read them. GT4_PAD_SAMPLING=frame (measurement): once a pass, as
    // before.
    const bool padsWhileWaiting = !options.fixedStep && [] {
        const char* e = std::getenv("GT4_PAD_SAMPLING");
        return !(e && std::strcmp(e, "frame") == 0);
    }();
    // ...and while Renderer::draw waits for the frame slot or a swapchain image (vsync without a frame-rate cap: the
    // pass waits there, not above), through its wait callback.
    uint64_t drawWaitCalls = 0;   // (GT4_TRACE_LATENCY's present lines: the wait callback's calls since the last)
    const auto padsInDraw = [&] {
        ++drawWaitCalls;
        if (gs && gs->live()) {
            SDL_PumpEvents();   // (the keyboard's and the gamepads' state; their events stay queued)
            sendPads(true);
        }
    };
    if (padsWhileWaiting) renderer->setWaitCallback(padsInDraw);
    while (running) {
        for (; nextScriptedKey < scriptedKeys.size() && scriptedKeys[nextScriptedKey].frame <= frames; ++nextScriptedKey) {
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.key = scriptedKeys[nextScriptedKey].key;
            key.key.down = true;
            SDL_PushEvent(&key);
        }
        {
            GT4_STAGE_SCOPE("main: window events");   // (the polls below pump again: little left to do then)
            SDL_PumpEvents();
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (traceLatency) {
                const double at = static_cast<double>(event.common.timestamp) * 1e-3 + ticksToQpcUs;
                if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && !event.key.repeat)
                    std::printf("latency: input key %d %s at %.0f\n", static_cast<int>(event.key.scancode),
                                event.key.down ? "down" : "up", at);
                else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
                    std::printf("latency: input button %d %s at %.0f\n", static_cast<int>(event.gbutton.button),
                                event.gbutton.down ? "down" : "up", at);
                else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
                    std::printf("latency: input axis %d %d at %.0f\n", static_cast<int>(event.gaxis.axis),
                                static_cast<int>(event.gaxis.value), at);
                std::fflush(stdout);   // (whole lines: the game's log shares the file)
            }
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: running = false; break;
            case SDL_EVENT_WINDOW_MINIMIZED: renderer->suspend(true); break;
            case SDL_EVENT_WINDOW_RESTORED: renderer->suspend(false); renderer->resize(); break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: renderer->resize(); break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                std::printf("Window moved to %s\n", gt4::windowDisplayName(sdlWindow).c_str());
                renderer->displayChanged();
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                // The PS2's two ports: a gamepad takes the port it held last, else the first free one (pad 1 has the
                // keyboard too). A session's are its virtual pads.
                pads.added(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                pads.removed(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                SDL_Gamepad* gamepad = pads.gamepad(0);
                if (!menuEnabled || !gamepad || SDL_GetGamepadID(gamepad) != event.gbutton.which) break;
                const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
                const bool chord = (button == SDL_GAMEPAD_BUTTON_START && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK)) ||
                                   (button == SDL_GAMEPAD_BUTTON_BACK && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START));
                if (button == SDL_GAMEPAD_BUTTON_GUIDE || chord) { openMenu(!menu.isOpen()); break; }
                if (menu.isOpen()) { menuButton(button); break; }
                if (!gsMode) {
                    const std::string change = gamepadCommand(settings, button);
                    if (!change.empty()) { std::printf("%s\n", change.c_str()); renderer->showMessage(change); renderer->apply(settings); save(); }
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (menuHeld == static_cast<SDL_GamepadButton>(event.gbutton.button)) menuHeld = SDL_GAMEPAD_BUTTON_INVALID;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE && menuEnabled) {
                    if (!event.key.repeat) openMenu(!menu.isOpen());
                    break;
                }
                if (event.key.key == SDLK_ESCAPE) { running = false; break; }
                if (menu.isOpen()) {
                    using Input = gt4::SettingsMenu::Input;
                    switch (event.key.key) {
                    case SDLK_UP: case SDLK_W: menuInput(Input::Up); break;
                    case SDLK_DOWN: case SDLK_S: menuInput(Input::Down); break;
                    case SDLK_LEFT: case SDLK_A: menuInput(Input::Left); break;
                    case SDLK_RIGHT: case SDLK_D: menuInput(Input::Right); break;
                    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                        if (!(event.key.mod & SDL_KMOD_ALT)) menuInput(Input::Accept);
                        break;
                    case SDLK_BACKSPACE: menuInput(Input::Back); break;
                    case SDLK_PAGEUP: case SDLK_Q: menuInput(Input::PageUp); break;
                    case SDLK_PAGEDOWN: case SDLK_E: menuInput(Input::PageDown); break;
                    default: break;
                    }
                    if (!(event.key.key >= SDLK_F1 && event.key.key <= SDLK_F12) &&
                        !(event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)))
                        break;
                }
                if (event.key.key == SDLK_LEFT) keyboardStick = -1.0f;
                if (event.key.key == SDLK_RIGHT) keyboardStick = 1.0f;
                if (event.key.key == SDLK_SPACE) keyboardAction = true;
                if (interactive && !event.key.repeat) {
                    const std::string change = keyCommand(settings, event.key, gsMode);
                    if (!change.empty()) {
                        std::printf("%s\n", change.c_str());
                        renderer->showMessage(change);
                        renderer->apply(settings);
                        if (display) display->apply(settings);
                        save();
                    }
                }
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) keyboardStick = 0.0f;
                if (event.key.key == SDLK_SPACE) keyboardAction = false;
                break;
            default: break;
            }
        }
        if (!running) break;
        if (session) {
            running &= session->tick(*renderer, sdlWindow, frames);
            if (!running) break;
            // The session's plugs and unplugs reach the ports in this frame, not when SDL's events are polled in the
            // next one: a first press of a newly plugged pad would otherwise be lost, and an unplugged one read for a
            // frame (lane C's review of D13). The events that follow find the work done (added and removed are
            // idempotent).
            for (int p = 0; p < 2; ++p) {
                const SDL_JoystickID id = session->padId(p);
                if (id == sessionPads[p]) continue;
                if (sessionPads[p]) pads.removed(sessionPads[p]);
                if (id) pads.added(id);
                sessionPads[p] = id;
            }
        }
        // display.gpu changed (key, session or settings): choose the device again with a new renderer.
        if (settings.gpu != activeGpu) {
            std::printf("display.gpu = %s: recreating the renderer\n", settings.gpu.c_str());
            renderer->abandonReadbacks();   // (the game's asynchronous reads answered first)
            sendReadAnswers();
            renderer.reset();
            renderer = makeRenderer(settings.gpu);
            activeGpu = settings.gpu;
            if (gs) renderer->useGameFrame(true);
            if (padsWhileWaiting) renderer->setWaitCallback(padsInDraw);
        }
        // Presenting only the game's frames (no frame interpolation, no frame-rate cap, the menu closed): the loop
        // waits for the next one rather than presenting the same picture again at the display's refresh rate (on a
        // 120 Hz handheld, twice the work for nothing). GT4_PRESENT_EVERY_REFRESH=1: every refresh, as before.
        static const bool everyRefresh = [] { const char* e = std::getenv("GT4_PRESENT_EVERY_REFRESH"); return e && std::strcmp(e, "0"); }();
        const bool followGame = gs && gs->live() && windowed && !lockstep && !everyRefresh && !settings.frameInterpolation &&
                                settings.fpsLimit == 0 && !menu.isOpen();
        bool freshFrame;
        {
            GT4_STAGE_SCOPE("main: waiting for the game's frames");
            if (!followGame) {
                freshFrame = true;
            } else if (!padsWhileWaiting) {
                freshFrame = gs->waitNativeFrames(30);
            } else {
                // (SDL_DelayNS: a high-resolution wait; a 1 ms condition wait sleeps a whole timer tick, 15.6 ms on
                // Windows, and the pads were then sampled about once a frame again)
                freshFrame = false;
                for (int ms = 0; ms < 30 && !(freshFrame = gs->waitNativeFrames(0)); ++ms) {
                    SDL_PumpEvents();   // (the keyboard's and the gamepads' state; their events stay queued)
                    sendPads(true);
                    SDL_DelayNS(1000000);
                }
            }
        }
        if (!freshFrame) ++passesWithoutFrame;
        bool servedRead = false;   // a read of drawn pixels answered in this pass
        // The game's frames: in lockstep replay one VBlank per rendered frame; otherwise the newest one.
        if (gs) {
            if (lockstep) gs->advance();
            if (gs->takeNativeFrames(nativeFrames)) {
                GT4_STAGE_SCOPE("main: frames to the renderer");
                takeCars(nativeFrames);
                for (const auto& list : nativeFrames) {
                    freshPending = freshPending || list.displayed;
                    if (list.displayed) newestAdded = std::max(newestAdded, list.vblank);
                }
                renderer->addNativeFrames(nativeFrames);
            }
            // A read of drawn pixels the game waits for (the car thumbnails it renders and keeps): the lists up
            // to it are drawn now and the pixels read back. Only here, between frames, never inside Renderer::draw
            // (CONTRACTS b.6's read order): GsnScene::read waits for the device, then records through its own render
            // graph, while a frame's graph is never between its build and its execution.
            gt4::gsn::ReadRequest read;
            if (gs->takeReadRequest(read)) {
                GT4_STAGE_SCOPE("main: a read the game waits for");
                servedRead = true;
                if (gs->takeNativeFrames(nativeFrames)) {
                    takeCars(nativeFrames);
                    renderer->addNativeFrames(nativeFrames);
                }
                std::vector<uint8_t> pixels;
                const bool ok = renderer->readGsTarget(read, pixels);
                gs->completeRead(ok, std::move(pixels));
            }
            sendReadAnswers();
            gs->setHdTextures(settings.hdTextures);
            gs->setSceneSplit(gt4::sceneSplitWanted(settings));   // (B6's ids for F2b's lamps, B7/B8's views; live and replays)
            gs->sendRequests();
            if (gs->live()) {
                gs->setPaused(menu.isOpen());
                gs->setGameOption(0, static_cast<uint32_t>(settings.aiCatchUp));   // runtime.h kOptionAiCatchUp
                gs->setGameOption(1, static_cast<uint32_t>(settings.mirror));      // kOptionMirrorDetail
                gs->setGameOption(2, static_cast<uint32_t>(settings.modelDetail)); // kOptionModelDetail
                gs->setGameOption(3, static_cast<uint32_t>(settings.raceFrames));  // kOptionRaceFrames
                // kOptionSceneRecords (CONTRACTS a.1): 1 full, else 2 the Vehicles only for the haptics (D14), else 0
                gs->setGameOption(5, gt4::sceneRecordsWanted(settings) ? 1u
                                     : settings.hapticsMode == gt4::HapticsMode::Physics ? 2u : 0u);
                {   // kOptionAspect: the content's shape now (aspect.ratio, or the window's)
                    const VkExtent2D e = renderer->drawableExtent();
                    const float a = settings.aspect == gt4::AspectRatio::Window ? (e.height ? float(e.width) / float(e.height) : 0.0f)
                                                                               : gt4::aspectValue(settings.aspect);
                    gs->setGameOption(4, a > 0.0f ? static_cast<uint32_t>(a * 1000.0f + 0.5f) : 0u);
                }
                gs->setGameOption(6, static_cast<uint32_t>(settings.triggerTravel)); // kOptionTriggerTravel
                // kOptionSteerBand: GT4's steering band, compensated game-side while a race runs (runtime/pad_steer.h); per
                // mille, 0 = off, and always 0 in original
                gs->setGameOption(7, settings.inputResponse == gt4::InputResponse::Modern
                                         ? static_cast<uint32_t>(std::lround(settings.steerGameBand * 10.0f)) : 0u);
                // kOptionHostMixer: audio.mixer (platform/audio/host_mixer.h, PLAN.md D5)
                gs->setGameOption(8, settings.audioMixer == gt4::AudioMixer::Host ? 1u : 0u);
                gs->setGameOption(9, static_cast<uint32_t>(settings.uiEntry));   // kOptionUiEntry (MENU_DESIGN 3.2)
                sendPads(false);
                // The pads' motors and triggers: GT4's own rumble (its Options > Vibration decides whether it sends any;
                // D2) and, with input.haptics = physics, what the player's car does (D14, platform/input/haptics.h),
                // from the newest Vehicle record (a quarter of a second old at most; none with the menu open or the pad
                // held back). A session records both (it has no real pad).
                for (int p = 0; p < 2; ++p) {
                    const uint32_t v = g_rumble[p].load();
                    const Uint64 now = SDL_GetTicks();
                    gt4::input::HapticsOut h;
                    const gt4::Settings& ps = pads.settingsFor(p, settings);   // (with the pad's own, D13)
                    if (ps.hapticsMode == gt4::HapticsMode::Physics && playerCarAt[p] && now - playerCarAt[p] < 250 &&
                        !menu.isOpen() && !padHeldBack)
                        h = gt4::input::haptics(playerCar[p], {ps.hapticsStrength / 100.0f, ps.triggerEffects, ps.hapticsUndersteer,
                                                               ps.hapticsLockUp, ps.hapticsWheelspin, ps.hapticsKerbs});
                    if (session) {
                        if (v != rumbleApplied[p])
                            session->record("rumble", "\"port\":" + std::to_string(p) + ",\"small\":" +
                                                          std::to_string((v >> 8) & 1) + ",\"large\":" + std::to_string(v & 0xFF));
                        rumbleApplied[p] = v;
                        const int now4[4] = {static_cast<int>(std::lround(h.low * 100)), static_cast<int>(std::lround(h.high * 100)),
                                             static_cast<int>(std::lround(h.triggerLeft * 100)),
                                             static_cast<int>(std::lround(h.triggerRight * 100))};
                        if (std::memcmp(now4, hapticsRecorded[p], sizeof now4) != 0) {
                            std::memcpy(hapticsRecorded[p], now4, sizeof now4);
                            session->record("haptics", "\"port\":" + std::to_string(p) + ",\"low\":" + std::to_string(now4[0]) +
                                                           ",\"high\":" + std::to_string(now4[1]) + ",\"left\":" +
                                                           std::to_string(now4[2]) + ",\"right\":" + std::to_string(now4[3]) +
                                                           ",\"vblank\":" + std::to_string(gs->vblanks()));
                        }
                    } else {
                        pads.feel(p, v, h, now);
                    }
                }
            }
            renderer->setStatus(gs->status());
            if (gs->refused())   // (PLAN.md P1: the stream path that drew such captures is gone)
                throw std::runtime_error("this capture holds GIF / VIF records, a format the host no longer reads: record it again");
            if (!options.game.empty() && gs->finished()) {
                std::printf("The game has exited; closing the host\n");
                gameExited = true;
                running = false;
                break;
            }
        }
        // The settings menu: held directions repeat; the view follows the settings (keys change them too).
        if (menu.isOpen()) {
            using Input = gt4::SettingsMenu::Input;
            if (SDL_Gamepad* gamepad = pads.gamepad(0)) {
                const float y = static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f;
                const int stick = y < -0.6f ? -1 : y > 0.6f ? 1 : 0;
                if (stick != stickHeld) {
                    stickHeld = stick;
                    if (stick) menuInput(stick < 0 ? Input::Up : Input::Down);
                    menuRepeatAt = SDL_GetTicks() + 350;
                } else if (stick && SDL_GetTicks() >= menuRepeatAt) {
                    menuInput(stick < 0 ? Input::Up : Input::Down);
                    menuRepeatAt = SDL_GetTicks() + 110;
                }
            }
            if (menuHeld != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetTicks() >= menuRepeatAt) {
                menuButton(menuHeld);
                menuRepeatAt = SDL_GetTicks() + 110;
            }
        }
        if (!running) break;
        if (menu.isOpen()) {
            const gt4::MenuView view = menu.view(settings);
            renderer->setMenu(&view);
        } else {
            renderer->setMenu(nullptr);
        }
        // Controls are sampled once per frame here; the game will sample its input once per tick.
        if (session) {
            renderer->setControls(session->horizontal(), session->action());
        } else if (interactive) {
            float stick = keyboardStick;
            bool action = keyboardAction;
            if (SDL_Gamepad* gamepad = pads.gamepad(0)) {
                const float axis = static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)) / 32768.0f;
                if (std::fabs(axis) > 0.12f) stick = axis;
                action |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            }
            renderer->setControls(stick, action);
        } else {
            renderer->setControls(static_cast<float>(std::sin(static_cast<double>(frames) / 54.0)), false);
        }

        if (options.smoke && SDL_GetTicks() - started > 30000) throw std::runtime_error("Rendering smoke test timed out");
        if (options.smoke && frames >= 30 && !firstResize) {
            require(SDL_SetWindowSize(sdlWindow, 800, 450), "SDL_SetWindowSize");
            renderer->resize();
            firstResize = true;
        }
        const auto drawable = renderer->drawableExtent();
        if (options.smoke && frames >= 60 && drawable.width == 800 && drawable.height == 450 && !minimized) {
            require(SDL_MinimizeWindow(sdlWindow), "SDL_MinimizeWindow");
            restoreAt = SDL_GetTicks() + 200;
            minimized = true;
            renderer->suspend(true);
        }
        if (minimized && !restored && SDL_GetTicks() >= restoreAt) {
            require(SDL_RestoreWindow(sdlWindow), "SDL_RestoreWindow");
            require(SDL_SetWindowSize(sdlWindow, 1280, 720), "SDL_SetWindowSize");
            renderer->resize();
            restored = true;
            renderer->suspend(false);
        }
        // Some desktop compositors acknowledge minimize asynchronously. Keep the finite smoke test paused until
        // its restore request, even if frames are fast.
        if (minimized && !restored) {
            sawMinimized = true;
            if (renderer->draw(0.0f)) throw std::runtime_error("Renderer presented while suspended");
            SDL_Delay(10);
            continue;
        }

        // Every 10 s with a live game (host.log in a package): the presentation and game rates and what a frame
        // costs, CPU (host and game processes, every thread) and GPU.
        if (gs && gs->live()) {
            const Uint64 ticks = SDL_GetTicks();
            if (!perf.at) {
                uint32_t waits, waitMs;
                gs->hostWaits(waits, waitMs);
                perf = {ticks + 10000, frames, gs->vblanks(), gt4::gs::processCpuMilliseconds(), gs->gameCpuSeconds() * 1000.0, perfGpu,
                        gs->drawnFrames(), freshShown, waits, waitMs};
            } else if (ticks >= perf.at) {
                const double seconds = (ticks - perf.at + 10000) / 1000.0;
                const uint32_t shown = frames - perf.frames;
                const uint64_t game = gs->vblanks() - perf.vblanks;
                const double host = gt4::gs::processCpuMilliseconds(), gameCpu = gs->gameCpuSeconds() * 1000.0;
                std::printf("perf: %.1f presented/s, %.1f game frames/s; CPU host %.2f ms/presented (%.0f%% of a core), "
                            "game %.2f ms/game frame (%.0f%% of a core); GPU %.2f ms/presented%s\n",
                            shown / seconds, game / seconds, shown ? (host - perf.host) / shown : 0.0,
                            (host - perf.host) / (seconds * 10.0), game ? (gameCpu - perf.game) / double(game) : 0.0,
                            (gameCpu - perf.game) / (seconds * 10.0), shown ? (perfGpu - perf.gpu) / shown : 0.0,
                            followGame ? "; presenting the game's frames" : "");
                // "game frames" above are VBlanks (the game's clock, 60 whether it draws or not): what the game drew,
                // and how many presents showed a frame it drew since the one before (repeats not counted).
                // ... and the game's last-resort waits for a host too far behind (runtime/link.cpp link_wait_for_host).
                uint32_t waits, waitMs;
                gs->hostWaits(waits, waitMs);
                std::printf("perf: game drew %.1f frames/s, %.1f new frames shown/s; the game waited for the host %u times (%u ms)\n",
                            (gs->drawnFrames() - perf.drawn) / seconds, (freshShown - perf.fresh) / seconds, waits - perf.waits,
                            waitMs - perf.waitMs);
                std::fflush(stdout);
                perf = {ticks + 10000, frames, gs->vblanks(), host, gameCpu, perfGpu, gs->drawnFrames(), freshShown, waits, waitMs};
            }
        }
        if (!freshFrame) continue;   // nothing new from the game: no frame to present
        // Woken by a read alone (the list cut at it displays nothing): presenting would show the last picture again,
        // and wait a refresh for it with vsync, while the rest of the game's frame is on its way (the Odin's race with
        // the flare pass's read every frame: 88 presents a second, 53 of them new).
        if (followGame && servedRead && !freshPending) continue;

        // The simulation clock: fixed 60 Hz ticks. Rendering only observes the result and interpolates.
        const Uint64 now = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(now - previous) / frequency;
        previous = now;
        float alpha = 1.0f;
        float delta = static_cast<float>(elapsed);
        if (gs) {
            // The game owns its clock; nothing to advance here.
        } else if (options.fixedStep) {
            renderer->scene().tick();
            delta = static_cast<float>(kTickSeconds);
        } else {
            accumulator += std::min(elapsed, 0.25);
            int steps = 0;
            while (accumulator >= kTickSeconds && steps < 8) { renderer->scene().tick(); accumulator -= kTickSeconds; ++steps; }
            if (steps == 8) accumulator = 0.0;
            alpha = static_cast<float>(accumulator / kTickSeconds);
        }
        renderer->scene().setInterpolation(alpha);
        if (!options.capture.empty() && frames + 1 == options.frames) renderer->capture(options.capture);
        else if (!options.capture.empty() && options.captureEvery && (frames + 1) % options.captureEvery == 0) {
            const auto dot = options.capture.find_last_of('.');
            char number[16];
            std::snprintf(number, sizeof number, "_%06u", frames + 1);
            renderer->capture(dot == std::string::npos ? options.capture + number
                                                       : options.capture.substr(0, dot) + number + options.capture.substr(dot));
        }
        if (!options.captureFrames.empty() && options.captureFrames.contains(frames + 1)) {
            char name[32];
            std::snprintf(name, sizeof name, "frame_%06u.ppm", frames + 1);
            renderer->capture((std::filesystem::path(options.captureDir) / name).string());
        }

        const Uint64 frameStarted = SDL_GetPerformanceCounter();
        bool presented = false;
        try {
            presented = renderer->draw(delta);
            if (gs) gs->setFrameDrawn(newestAdded);   // every list added was drawn (or skipped) by this draw
            sendReadAnswers();
        } catch (const PresentationLost& lost) {
            // The window is now on a display this GPU cannot present to: choose a device again.
            std::fprintf(stderr, "%s\n", lost.what());
            if (settings.gpu != "auto") throw;
            renderer->abandonReadbacks();
            sendReadAnswers();
            renderer.reset();
            renderer = makeRenderer("auto");
            continue;
        }
        if (presented && traceLatency) {
            std::printf("latency: present VBlank %llu at %.0f\n", static_cast<unsigned long long>(newestAdded),
                        qpcUs(SDL_GetPerformanceCounter()));
            if (drawWaitCalls)
                std::printf("latency: draw waited %llu slices\n", static_cast<unsigned long long>(drawWaitCalls));
            drawWaitCalls = 0;
            std::fflush(stdout);
        }
        if (presented && freshPending) {
            ++freshShown;
            freshPending = false;
        }
        if (presented) {
            ++frames;
            perfGpu += renderer->gpuMilliseconds();
            // --profile-json: the frame pacing and the GPU's time, a line a presented frame (tools/perf_report.py).
            if (!options.profileJson.empty()) {
                static std::FILE* profile = std::fopen(options.profileJson.c_str(), "w");
                static Uint64 lastPresent = 0;
                static uint64_t lastSkipped = 0;
                const Uint64 now = SDL_GetPerformanceCounter();
                const uint64_t skipped = renderer->sceneCounters().framesSkipped;
                if (profile) {
                    // the host's CPU stages since the last present (GT4_STAGE_SCOPE; GT4_CPU_SCOPE with GT4_CPU_PROFILE=1)
                    static std::vector<uint64_t> cpuLast;
                    std::string stages;
                    // (labels of any length, quotes and backslashes made plain: the line stays one JSON object)
                    auto item = [](std::string& list, const char* label, const char* field, double value,
                                   const char* field2 = nullptr, double value2 = 0.0) {
                        char number[32];
                        list += list.empty() ? "{\"name\":\"" : ",{\"name\":\"";
                        for (const char* c = label; *c; ++c) list += (*c == '"' || *c == '\\') ? '\'' : *c;
                        list += '"';
                        for (const auto& [f, v] : {std::pair{field, value}, std::pair{field2, value2}}) {
                            if (!f) continue;
                            std::snprintf(number, sizeof number, "%.1f", v);
                            list += std::string(",\"") + f + "\":" + number;
                        }
                        list += '}';
                    };
                    gt4::cpuprofile::since(cpuLast, [&](const char* label, uint64_t ns) {
                        item(stages, label, "cpu_us", static_cast<double>(ns) / 1000.0);
                    });
                    // the GS back end's GPU stages of its last finished frame (the GT4_GPU_PROFILE labels)
                    renderer->setStageTiming(true);
                    std::string gsPasses;
                    for (const auto& [label, ms] : renderer->gsStages())   // (-1: a pass of it not timed)
                        item(gsPasses, label.c_str(), "gpu_us", ms < 0.0 ? -1.0 : ms * 1000.0);
                    // the display pipeline's render graph passes in its last finished frame (CONTRACTS b.7): GPU -1 when
                    // not timed; several passes can share a name (bloom: one a mip), perf_report.py adds them up
                    std::string passes;
                    for (const auto& p : renderer->passTimings())
                        item(passes, p.name.c_str(), "gpu_us", p.gpuMicroseconds, "cpu_us", p.cpuMicroseconds);
                    std::fprintf(profile, "{\"frame\":%u,\"present_ms\":%.3f,\"draw_ms\":%.3f,\"gpu_ms\":%.3f,\"skipped\":%llu,"
                                          "\"passes\":[%s],\"transient_bytes\":%llu,\"gs_passes\":[%s],\"cpu_stages\":[%s]}\n",
                                 frames, lastPresent ? static_cast<double>(now - lastPresent) * 1000.0 / frequency : 0.0,
                                 static_cast<double>(now - frameStarted) * 1000.0 / frequency, renderer->gpuMilliseconds(),
                                 static_cast<unsigned long long>(skipped - lastSkipped), passes.c_str(),
                                 static_cast<unsigned long long>(renderer->transientBytes()), gsPasses.c_str(), stages.c_str());
                    std::fflush(profile);
                }
                lastPresent = now;
                lastSkipped = skipped;
            }
            // The hitch log (GT4_HITCH_LOG=0: off): with a live game, one line for a present more than 25 ms after the
            // one before, or that skipped game frames, or after which the game waited for the host: its time, the GPU's
            // last frame, and what can make a frame slow (frames skipped, textures decoded and uploaded, render targets and pipelines
            // created). At most 20 lines a second. (CPU time: the perf line; the process clocks tick by 15.6 ms, too
            // coarse for one frame. The GPU's passes: GT4_GPU_PROFILE=2. Auto exposure is not applied to game frames.)
            static const bool hitchLog = [] { const char* e = std::getenv("GT4_HITCH_LOG"); return !(e && *e == '0'); }();
            if (hitchLog && gs && gs->live()) {
                static struct {
                    Uint64 at = 0, second = 0;
                    uint32_t lines = 0, waits = 0, waitMs = 0;
                    gt4::GsnScene::Counters c;
                } hitch;
                const Uint64 now = SDL_GetPerformanceCounter();
                const gt4::GsnScene::Counters c = renderer->sceneCounters();
                uint32_t waits, waitMs;
                gs->hostWaits(waits, waitMs);
                if (hitch.at) {
                    const double ms = static_cast<double>(now - hitch.at) * 1000.0 / frequency;
                    const uint64_t skipped = c.framesSkipped - hitch.c.framesSkipped;
                    if (now - hitch.second >= frequency) hitch.second = now, hitch.lines = 0;
                    if ((ms > 25.0 || skipped || waits != hitch.waits) && hitch.lines < 20) {
                        ++hitch.lines;
                        // A long one (100 ms and more) also names where the threads spent the time since the last
                        // present: the main loop, the render calls, the GS thread and the front end (GT4_STAGE_SCOPE).
                        std::string stages;
                        if (ms >= 100.0)
                            stages = "; " + std::to_string(passesWithoutFrame) + " passes without a new frame, " +
                                     std::to_string(drawsWithoutPresent) + " draws without a present; stages: " +
                                     gt4::cpuprofile::busiest(stagesBefore, 5.0, 8) + "; texture heap " +
                                     renderer->textureHeapSummary();
                        std::printf("hitch: %.1f ms since the last present; GPU %.2f ms; %llu frames skipped; "
                                    "%llu textures (%llu KB), %llu new targets, %llu pipelines created; the game waited %u times (%u ms)%s\n",
                                    ms, renderer->gpuMilliseconds(),
                                    static_cast<unsigned long long>(skipped), static_cast<unsigned long long>(c.textures - hitch.c.textures),
                                    static_cast<unsigned long long>((c.textureBytes - hitch.c.textureBytes) / 1024),
                                    static_cast<unsigned long long>(c.targets - hitch.c.targets),
                                    static_cast<unsigned long long>(c.pipelines - hitch.c.pipelines), waits - hitch.waits,
                                    waitMs - hitch.waitMs, stages.c_str());
                    }
                }
                if (gt4::cpuprofile::stagesMeasured()) gt4::cpuprofile::busiest(stagesBefore, 1e18, 0);   // from this present on
                passesWithoutFrame = drawsWithoutPresent = 0;
                hitch.at = now, hitch.c = c, hitch.waits = waits, hitch.waitMs = waitMs;
            }
            if (session)
                session->frame(frames, static_cast<double>(SDL_GetPerformanceCounter() - frameStarted) * 1000.0 / frequency,
                               renderer->gpuMilliseconds(), renderer->drawableExtent().width, renderer->drawableExtent().height);
            const auto size = renderer->drawableExtent();
            if (options.frames && frames >= options.frames &&
                (!options.smoke || (restored && sawMinimized && size.width == 1280 && size.height == 720)))
                running = false;
            if (options.smoke) SDL_Delay(8);
        } else {
            ++drawsWithoutPresent;
            SDL_Delay(10);
        }
        // Frame-rate cap (display.fps_limit): paces presentation only; the tick rate above is unaffected.
        // Without a window, a live game or a real-time replay is shown at its own 60 Hz.
        const bool paceToGame = gs && !windowed && !lockstep;
        if ((settings.fpsLimit > 0 && windowed) || paceToGame) {
            // (headless, display.fps_limit sets the presentation rate: the decoupled presentation without a window)
            const Uint64 interval = settings.fpsLimit > 0 ? 1000000000ull / static_cast<Uint64>(settings.fpsLimit) : 16666667ull;
            nextFrame += interval;
            const Uint64 current = SDL_GetTicksNS();
            if (nextFrame > current) {
                // A read the game waits for ends the wait: it is taken at once (above, in the next pass), not after the
                // rest of the frame's interval (the game asks its reads one after another: the race's 6 car icons, 12 in
                // one list, waited about 6 ms each here). The presentation's pace goes on from nextFrame.
                // Cut into 1 ms sleeps while more than 2 ms remain; the tail is one SDL_DelayPrecise, so its own
                // compensation for the timer's overshoot keeps the present on time (a read then waits 2 ms at most).
                // A pass woken by a read presents once more in that interval (without a window nothing is shown: one
                // draw of GPU time); the next wait then reaches nextFrame + interval, so the rate stays 60 a second.
                // The pads are sampled in these sleeps too (PLAN.md D11, as while waiting for the game's frame): with
                // a cap, the pass would otherwise sample them once an interval, up to a whole interval old.
                if (gs && gs->live()) {
                    Uint64 now = current;
                    while (nextFrame > now && nextFrame - now > 2000000ull && !gs->readRequested()) {
                        SDL_DelayPrecise(1000000ull);
                        if (padsWhileWaiting) {
                            SDL_PumpEvents();   // (the keyboard's and the gamepads' state; their events stay queued)
                            sendPads(true);
                        }
                        now = SDL_GetTicksNS();
                    }
                    if (nextFrame > now && !gs->readRequested()) SDL_DelayPrecise(nextFrame - now);
                } else {
                    SDL_DelayPrecise(nextFrame - current);
                }
            } else {
                nextFrame = current;
            }
        }
    }
    if (options.frames && frames < options.frames && !gameExited)
        throw std::runtime_error("Window closed before the requested frames were presented");
    if (options.smoke && (!firstResize || !minimized || !restored || !sawMinimized))
        throw std::runtime_error("Rendering smoke test did not complete window transitions");
    // the main thread's CPU time a frame: the window, the render thread's recording and submission
    const double mainMs = frames ? gt4::gs::threadCpuMilliseconds() / double(frames) : 0.0;
    std::printf("%s %u frames on %s (%s); %s; GPU %.3f ms/frame; main thread CPU %.3f ms/frame%s\n", windowed ? "Presented" : "Rendered", frames,
                renderer->gpu().name.c_str(), renderer->gpu().type.c_str(), renderer->pipelineDescription().c_str(),
                renderer->averageGpuMilliseconds(), mainMs, options.smoke ? "; resize/minimize/restore smoke test OK" : "");
    std::fflush(stdout);
    // A teardown that hangs (seen after long headless runs of the game) must not keep scripts waiting.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::fprintf(stderr, "Shutdown did not finish in 30 s; exiting\n");
        std::fflush(nullptr);
        std::_Exit(4);
    }).detach();
    save();
    // A game that ended by itself with an error makes the host fail too (scripts and sessions see it).
    if (gameExited && gs && gs->gameExitCode() != 0) {
        std::fprintf(stderr, "gt4_game failed with exit code %d\n", gs->gameExitCode());
        return 3;
    }
    return 0;
}

}  // namespace

// A packaged game (tools/package_game.py): started with no mode, the host runs gt4_game.exe from its own folder on
// game/CORE.GT4.elf and the disc image beside it, with its settings, saves and the game's log there too, so the
// folder can be copied anywhere (another PC, a Steam Deck or an ARM handheld under Proton). --headless --frames N
// checks a package without a window.
void usePackagedGame(Options& o) {
    if (!o.game.empty() || !o.replay.empty() || o.probe || o.smoke || o.listGpus || o.listDisplays || o.listOptions ||
        !o.sessionDirectory.empty())
        return;
    const char* basePath = SDL_GetBasePath();
    if (!basePath) return;
    namespace fs = std::filesystem;
    const auto utf8 = [](const fs::path& p) { const std::u8string s = p.u8string(); return std::string(s.begin(), s.end()); };
    const std::string baseText = basePath;
    const fs::path base(std::u8string(baseText.begin(), baseText.end()));
    const fs::path game = base / "gt4_game.exe", elf = base / "game" / "CORE.GT4.elf";
    std::error_code error;
    if (!fs::is_regular_file(game, error)) return;
    if (!o.importSave.empty() || !o.exportSave.empty()) {   // in a package, the package's own save folder
        if (o.saves.empty()) o.saves = utf8(base / "saves");
        return;
    }
    fs::path iso;
    for (const auto& entry : fs::directory_iterator(base / "game", error)) {
        std::string extension = utf8(entry.path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension == ".iso") {
            iso = entry.path();
            break;
        }
    }
    // A release package comes without game data: the player puts their own disc image in game/, and the executable the
    // game loads is made from it on the first start.
    if (!fs::is_regular_file(elf, error)) {
        const std::string problem = iso.empty()
            ? "No disc image found. Put an image of your own Gran Turismo 4 disc (USA, SCUS-97328, version 1.01) as an "
              ".iso file in the folder\n\n" + utf8(base / "game") + "\n\nand start gt4_host.exe again."
            : makeCoreElf(iso, elf);
        if (!problem.empty()) {
            if (!o.headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GT4 Native", problem.c_str(), nullptr);
            throw std::runtime_error(problem);
        }
        std::printf("made %s from %s\n", utf8(elf).c_str(), utf8(iso.filename()).c_str());
    }
    o.game = utf8(game);
    std::vector<std::string> args{"--elf", gamePathArgument(utf8(elf)), "--log", gamePathArgument(utf8(base / "game.log"))};
    if (!iso.empty()) args.insert(args.end(), {"--iso", gamePathArgument(utf8(iso))});
    o.gameArgs.insert(o.gameArgs.begin(), args.begin(), args.end());
    if (o.saves.empty()) {
        fs::create_directories(base / "saves", error);
        o.saves = utf8(base / "saves");
    }
    if (o.settingsPath.empty()) o.settingsPath = utf8(base / "settings.ini");
    // What the host prints (its start, the performance lines, errors) to host.log beside it: a handheld under
    // Proton shows no console.
#ifdef _WIN32
    const fs::path hostLog = base / "host.log";
    if (_wfreopen(hostLog.c_str(), L"w", stdout)) {
        _dup2(_fileno(stdout), _fileno(stderr));   // one file position for both
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
}

int main(int argc, char** argv) {
    try {
        Options options = parse(argc, argv);
        if (!options.captureDir.empty()) std::filesystem::create_directories(options.captureDir);
        usePackagedGame(options);
        return run(options);
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "GT4 native failed: %s\n", error.what());
        return 1;
    }
}
