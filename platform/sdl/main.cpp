// gt4_host: the SDL3 + Vulkan platform host. It runs the display pipeline on a test scene, or on the PS2's
// displayed frame from the GS back end: a live gt4_game.exe (--game) or a capture (--replay)
// (docs/RENDERING.md, docs/GS_BACKEND.md). Windowed, or headless (offscreen, no window or surface).
#include <volk.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include "display.h"
#include "gs_host.h"
#include "ps2_pad.h"
#include "renderer.h"
#include "session.h"
#include "settings.h"
#include "test_scene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kTickSeconds = 1.0 / 60.0;   // the game's simulation rate (docs/PLATFORM.md)

void require(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

const char* kUsage =
    "Usage: gt4_host [mode] [options]\n"
    "SDL3/Vulkan host: the display pipeline on a test scene, or on GT4's frames (--game, --replay).\n"
    "Modes (default: interactive window, Escape closes it):\n"
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
    "  --session-dir PATH   attach a virtual gamepad, process commands, record frames/audio/events\n"
    "  --no-audio           disable playback and WAV capture for a session\n"
    "  --fixed-step         advance exactly one 60 Hz tick per frame (default for headless and sessions)\n"
    "  --game PATH          start gt4_game.exe with a link and display its frames (GS back end)\n"
    "  --game-args \"ARGS\"  extra arguments for the game (after --link NAME)\n"
    "  --replay FILE        display a capture of the game's graphics stream (gt4_game --capture) instead;\n"
    "                       with --fixed-step (headless) one VBlank per rendered frame, else 59.94 Hz\n"
    "  --replay-loop        start the replay again at its end\n"
    "  --gs-ssaa N          GS super-sampling 1 (faithful, default), 2, 4, 8 or 16\n"
    "  --gs-hires           with --gs-ssaa 4 or more: scan out at twice the resolution (1280 x 896)\n"
    "Interactive runs read and save the settings file in the user's preference directory; scripted runs\n"
    "(--frames, --smoke-test, --session-dir, --headless) start from the defaults unless --settings is given.\n"
    "Keys: F1 readout, F2 preset, F3 anti-aliasing, F4 upscaler, F5 render quality, F6 aspect ratio,\n"
    "F7 tonemapper, F8 HUD layout, F9 vsync, F10 MSAA, F11/Alt+Enter fullscreen, B bloom, N depth of field,\n"
    "M motion blur, H shadows, R reflections, F fog, T texture filter, Y anisotropy, [ ] render scale,\n"
    "- = sharpness, G grain, V vignette, C aberration, P pause, arrows/left stick camera, Space action;\n"
    "Shift reverses a cycle.";

struct Options {
    bool probe = false, smoke = false, headless = false, listGpus = false, listDisplays = false, listOptions = false;
    bool fixedStep = false, audio = true, replayLoop = false, gsHires = false;
    uint32_t gsSsaa = 1;
    std::string game, replay;
    std::vector<std::string> gameArgs;
    uint32_t frames = 0;
    uint32_t captureEvery = 0;  // --capture-every: also every N frames, numbered
    VkExtent2D size{0, 0};   // headless output; 0 = the window size setting
    std::string gpu, settingsPath, sessionDirectory, capture;
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
        else if (a == "--no-audio") o.audio = false;
        else if (a == "--gpu") o.gpu = value(i);
        else if (a == "--settings") o.settingsPath = value(i);
        else if (a == "--session-dir") o.sessionDirectory = value(i);
        else if (a == "--capture") o.capture = value(i);
        else if (a == "--capture-every") {
            if (!parseUnsigned(value(i), o.captureEvery) || o.captureEvery == 0) throw std::invalid_argument("--capture-every requires a positive integer");
        }
        else if (a == "--game") o.game = value(i);
        else if (a == "--game-args") o.gameArgs = splitArguments(value(i));
        else if (a == "--replay") o.replay = value(i);
        else if (a == "--replay-loop") o.replayLoop = true;
        else if (a == "--gs-hires") o.gsHires = true;
        else if (a == "--gs-ssaa") {
            if (!parseUnsigned(value(i), o.gsSsaa) || (o.gsSsaa != 1 && o.gsSsaa != 2 && o.gsSsaa != 4 && o.gsSsaa != 8 && o.gsSsaa != 16))
                throw std::invalid_argument("--gs-ssaa takes 1, 2, 4, 8 or 16");
        }
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
    if (o.smoke) o.frames = 180;
    if (o.headless || !o.sessionDirectory.empty()) o.fixedStep = true;
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
        bool debugUtils = false;
        for (const auto& p : properties) debugUtils |= std::strcmp(p.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
        if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
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

// The game's pad 1 from the SDL gamepad (standard layout) and the keyboard (docs/GS_BACKEND.md, "Input").
gt4::ps2::PadInput readPad(SDL_Gamepad* gamepad) {
    using namespace gt4::ps2;
    PadInput in;
    auto press = [&](PadButton b, bool down) { if (down) in.button[b] = 1.0f; };
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        press(PadUp, keys[SDL_SCANCODE_UP]);
        press(PadDown, keys[SDL_SCANCODE_DOWN]);
        press(PadLeft, keys[SDL_SCANCODE_LEFT]);
        press(PadRight, keys[SDL_SCANCODE_RIGHT]);
        press(PadCross, keys[SDL_SCANCODE_X]);
        press(PadCircle, keys[SDL_SCANCODE_C]);
        press(PadSquare, keys[SDL_SCANCODE_Z]);
        press(PadTriangle, keys[SDL_SCANCODE_V]);
        press(PadL1, keys[SDL_SCANCODE_Q]);
        press(PadR1, keys[SDL_SCANCODE_E]);
        press(PadL2, keys[SDL_SCANCODE_1]);
        press(PadR2, keys[SDL_SCANCODE_3]);
        press(PadStart, keys[SDL_SCANCODE_RETURN]);
        press(PadSelect, keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_RSHIFT]);
        if (keys[SDL_SCANCODE_J]) in.leftX -= 1.0f;
        if (keys[SDL_SCANCODE_L]) in.leftX += 1.0f;
        if (keys[SDL_SCANCODE_I]) in.leftY -= 1.0f;
        if (keys[SDL_SCANCODE_K]) in.leftY += 1.0f;
    }
    if (gamepad) {
        auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(gamepad, b); };
        auto axis = [&](SDL_GamepadAxis a) { return static_cast<float>(SDL_GetGamepadAxis(gamepad, a)) / 32767.0f; };
        press(PadCross, button(SDL_GAMEPAD_BUTTON_SOUTH));
        press(PadCircle, button(SDL_GAMEPAD_BUTTON_EAST));
        press(PadSquare, button(SDL_GAMEPAD_BUTTON_WEST));
        press(PadTriangle, button(SDL_GAMEPAD_BUTTON_NORTH));
        press(PadSelect, button(SDL_GAMEPAD_BUTTON_BACK));
        press(PadStart, button(SDL_GAMEPAD_BUTTON_START));
        press(PadL3, button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
        press(PadR3, button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        press(PadL1, button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        press(PadR1, button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        press(PadUp, button(SDL_GAMEPAD_BUTTON_DPAD_UP));
        press(PadDown, button(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        press(PadLeft, button(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        press(PadRight, button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        // Analogue triggers drive the L2/R2 pressures (GT4's accelerator/brake can use them).
        in.button[PadL2] = std::max(in.button[PadL2], std::clamp(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f, 1.0f));
        in.button[PadR2] = std::max(in.button[PadR2], std::clamp(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f, 1.0f));
        auto stick = [](float v) { return std::fabs(v) < 0.08f ? 0.0f : v; };
        in.leftX = std::clamp(in.leftX + stick(axis(SDL_GAMEPAD_AXIS_LEFTX)), -1.0f, 1.0f);
        in.leftY = std::clamp(in.leftY + stick(axis(SDL_GAMEPAD_AXIS_LEFTY)), -1.0f, 1.0f);
        in.rightX = stick(axis(SDL_GAMEPAD_AXIS_RIGHTX));
        in.rightY = stick(axis(SDL_GAMEPAD_AXIS_RIGHTY));
    }
    return in;
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
    // The PS2 frame fills a 4:3 TV picture (640 x 448 with non-square pixels); --set aspect.ratio overrides.
    const bool gsMode = !options.game.empty() || !options.replay.empty();
    if (gsMode && settings.aspect == gt4::AspectRatio::Window) gt4::setOption(settings, "aspect.ratio", "4:3");
    for (const auto& [key, value] : options.sets) {
        const std::string error = gt4::setOption(settings, key, value);
        if (!error.empty()) throw std::invalid_argument("--set " + key + "=" + value + ": " + error);
    }
    if (!options.gpu.empty()) settings.gpu = options.gpu;
    if (options.listOptions) {
        for (const auto& o : gt4::describe(settings))
            std::printf("%s = %s\n    %s\n    %s\n", o.key.c_str(), o.value.c_str(), o.allowed.c_str(), o.help.c_str());
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
        return windowed ? std::make_unique<Renderer>(sdlWindow, instance.instance, gpu, settings)
                        : std::make_unique<Renderer>(instance.instance, headlessSize, gpu, settings);
    };
    std::unique_ptr<Renderer> renderer = makeRenderer(settings.gpu);
    std::string activeGpu = settings.gpu;
    if (options.probe) {
        std::printf("Probe OK: %s can present to %s\n", renderer->gpu().name.c_str(), gt4::windowDisplayName(sdlWindow).c_str());
        return 0;
    }

    // The game's frames: the GS back end (its own Vulkan device on the renderer's GPU) fed by a live game
    // or a capture. Creating it loads volk's globals for its own instance, so the renderer reloads its own.
    std::unique_ptr<gt4::gs::GsHost> gs;
    const bool lockstep = !options.replay.empty() && options.fixedStep;
    gt4::gs::DisplayFrame gsFrame;
    uint64_t gsSerial = 0;
    bool gameExited = false;
    if (gsMode) {
        gt4::gs::GsHost::Options gsOptions;
        gsOptions.gamePath = options.game;
        gsOptions.gameArgs = options.gameArgs;
        gsOptions.replayPath = options.replay;
        gsOptions.lockstep = lockstep;
        gsOptions.loop = options.replayLoop;
        gsOptions.backend.gpu = renderer->gpu().name;
        gsOptions.backend.superSampling = options.gsSsaa;
        gsOptions.backend.highResolutionScanout = options.gsHires;
        std::string error;
        gs = gt4::gs::GsHost::create(gsOptions, error);
        renderer->reloadVulkanFunctions();
        if (!gs) throw std::runtime_error(error);
        renderer->useGameFrame(true);
        if (window) SDL_SetWindowTitle(window->window, options.game.empty() ? "GT4 native host - replay" : "GT4 native host");
    }

    std::unique_ptr<Session> session;
    if (!options.sessionDirectory.empty()) {
        session = std::make_unique<Session>(options.sessionDirectory, options.audio, true);
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
    SDL_Gamepad* gamepad = nullptr;
    auto save = [&] {
        if (interactive && !settingsPath.empty()) {
            try { gt4::saveSettings(settings, settingsPath); }
            catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
        }
    };

    bool running = true, firstResize = false, minimized = false, restored = false, sawMinimized = false;
    uint32_t frames = 0;
    const Uint64 started = SDL_GetTicks();
    Uint64 restoreAt = 0;
    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;
    Uint64 nextFrame = SDL_GetTicksNS();
    float keyboardStick = 0.0f;
    bool keyboardAction = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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
                if (!gamepad && !session) gamepad = SDL_OpenGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (gamepad && SDL_GetGamepadID(gamepad) == event.gdevice.which) { SDL_CloseGamepad(gamepad); gamepad = nullptr; }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (interactive && !gsMode && gamepad && SDL_GetGamepadID(gamepad) == event.gbutton.which) {
                    const std::string change = gamepadCommand(settings, static_cast<SDL_GamepadButton>(event.gbutton.button));
                    if (!change.empty()) { std::printf("%s\n", change.c_str()); renderer->apply(settings); save(); }
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) { running = false; break; }
                if (event.key.key == SDLK_LEFT) keyboardStick = -1.0f;
                if (event.key.key == SDLK_RIGHT) keyboardStick = 1.0f;
                if (event.key.key == SDLK_SPACE) keyboardAction = true;
                if (interactive && !event.key.repeat) {
                    const std::string change = keyCommand(settings, event.key, gsMode);
                    if (!change.empty()) {
                        std::printf("%s\n", change.c_str());
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
        }
        // display.gpu changed (key, session or settings): choose the device again with a new renderer.
        if (settings.gpu != activeGpu) {
            std::printf("display.gpu = %s: recreating the renderer\n", settings.gpu.c_str());
            renderer.reset();
            renderer = makeRenderer(settings.gpu);
            activeGpu = settings.gpu;
            if (gs) {   // the GS back end keeps its own device; show its frames on the new renderer
                renderer->useGameFrame(true);
                if (!gsFrame.rgba.empty()) renderer->setGameFrame(gsFrame.width, gsFrame.height, gsFrame.rgba.data());
            }
        }
        // The game's frames: in lockstep replay one VBlank per rendered frame; otherwise the newest one.
        if (gs) {
            if (lockstep) gs->advance();
            if (gs->takeFrame(gsFrame, gsSerial)) renderer->setGameFrame(gsFrame.width, gsFrame.height, gsFrame.rgba.data());
            if (gs->live()) {
                uint8_t pad[18];
                gt4::ps2::encodePad(readPad(gamepad), pad);
                gs->sendPad(0, true, pad);
            }
            renderer->setStatus(gs->status());
            if (!options.game.empty() && gs->finished()) {
                std::printf("The game has exited; closing the host\n");
                gameExited = true;
                running = false;
                break;
            }
        }
        // Controls are sampled once per frame here; the game will sample its input once per tick.
        if (session) {
            renderer->setControls(session->horizontal(), session->action());
        } else if (interactive) {
            float stick = keyboardStick;
            bool action = keyboardAction;
            if (gamepad) {
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

        const Uint64 frameStarted = SDL_GetPerformanceCounter();
        bool presented = false;
        try {
            presented = renderer->draw(delta);
        } catch (const PresentationLost& lost) {
            // The window is now on a display this GPU cannot present to: choose a device again.
            std::fprintf(stderr, "%s\n", lost.what());
            if (settings.gpu != "auto") throw;
            renderer.reset();
            renderer = makeRenderer("auto");
            continue;
        }
        if (presented) {
            ++frames;
            if (session)
                session->frame(frames, static_cast<double>(SDL_GetPerformanceCounter() - frameStarted) * 1000.0 / frequency,
                               renderer->gpuMilliseconds(), renderer->drawableExtent().width, renderer->drawableExtent().height);
            const auto size = renderer->drawableExtent();
            if (options.frames && frames >= options.frames &&
                (!options.smoke || (restored && sawMinimized && size.width == 1280 && size.height == 720)))
                running = false;
            if (options.smoke) SDL_Delay(8);
        } else {
            SDL_Delay(10);
        }
        // Frame-rate cap (display.fps_limit): paces presentation only; the tick rate above is unaffected.
        // Without a window, a live game or a real-time replay is shown at its own 59.94 Hz.
        const bool paceToGame = gs && !windowed && !lockstep;
        if ((settings.fpsLimit > 0 && windowed) || paceToGame) {
            const Uint64 interval = paceToGame ? 16683350ull : 1000000000ull / static_cast<Uint64>(settings.fpsLimit);
            nextFrame += interval;
            const Uint64 current = SDL_GetTicksNS();
            if (nextFrame > current) SDL_DelayPrecise(nextFrame - current);
            else nextFrame = current;
        }
    }
    if (gamepad) SDL_CloseGamepad(gamepad);
    if (options.frames && frames < options.frames && !gameExited)
        throw std::runtime_error("Window closed before the requested frames were presented");
    if (options.smoke && (!firstResize || !minimized || !restored || !sawMinimized))
        throw std::runtime_error("Rendering smoke test did not complete window transitions");
    std::printf("%s %u frames on %s (%s); %s; GPU %.3f ms/frame%s\n", windowed ? "Presented" : "Rendered", frames,
                renderer->gpu().name.c_str(), renderer->gpu().type.c_str(), renderer->pipelineDescription().c_str(),
                renderer->averageGpuMilliseconds(), options.smoke ? "; resize/minimize/restore smoke test OK" : "");
    save();
    // A game that ended by itself with an error makes the host fail too (scripts and sessions see it).
    if (gameExited && gs && gs->gameExitCode() != 0) {
        std::fprintf(stderr, "gt4_game failed with exit code %d\n", gs->gameExitCode());
        return 3;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "GT4 native failed: %s\n", error.what());
        return 1;
    }
}
