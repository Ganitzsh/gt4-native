#pragma once
// User-facing display and image-quality settings of the native host (docs/RENDERING.md lists every
// option). One plain struct, a versioned INI file in the user's configuration directory, command-line
// overrides (--set section.key=value) and graphics presets. Settings only steer presentation: nothing
// here may feed back into game state (docs/PLATFORM.md, "Rendering and audio only observe"), except the
// [gameplay] section, whose options the game is sent explicitly (GT4_REC_HOST_CMD 4) and whose defaults
// are the original game's behaviour.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gt4 {

enum class WindowMode : uint8_t { Windowed, Borderless, Exclusive };
enum class VSync : uint8_t { On, Off, Adaptive, Mailbox };      // FIFO, IMMEDIATE, FIFO_RELAXED, MAILBOX
enum class AspectRatio : uint8_t { Window, R4_3, R16_9, R16_10, R21_9, R32_9 };
enum class HudLayout : uint8_t { Anchored, Centered, Stretched };
enum class QualityMode : uint8_t { Native, UltraQuality, Quality, Balanced, Performance, UltraPerformance, Custom };
enum class Upscaler : uint8_t { Bilinear, Bicubic, Lanczos, Fsr1, Fsr2 };
// The resolution GT4's graphics are drawn at by the native renderer (docs/NATIVE_RENDERER.md): the PS2's own
// (one render pixel per GS pixel, 640 x 448), or the render resolution.
enum class GsRenderer : uint8_t { Original, Native };
enum class AntiAliasing : uint8_t { Off, Fxaa, Smaa, SmaaT2x, Taa };
enum class TextureFilter : uint8_t { Point, Bilinear, Trilinear };
enum class Tonemapper : uint8_t { Original, Reinhard, Aces, AgX, AgXPunchy, PbrNeutral };
// Original: what the PS2 game draws (with the R1 renderer, the game's own GS output for that effect).
enum class EffectQuality : uint8_t { Original, Off, Low, Medium, High, Ultra };
enum class Preset : uint8_t { Original, Low, Medium, High, Ultra, Custom };
// The AI's catch-up (GT4's "slow car boost": cars behind sped up, cars ahead slowed down, by event).
enum class AiCatchUp : uint8_t { Original, Reduced, Off };

struct Settings {
    static constexpr int kVersion = 1;

    // [display]
    WindowMode windowMode = WindowMode::Windowed;
    std::string gpu = "auto";         // Vulkan device: auto, an index or part of a name (--list-gpus)
    int monitor = 0;                  // index in the display list (--list-displays); 0 is the primary
    int windowWidth = 1280, windowHeight = 720;
    int fullscreenWidth = 0, fullscreenHeight = 0;  // exclusive mode; 0 = the desktop mode
    float refreshRate = 0.0f;         // exclusive mode; 0 = highest available for the size
    VSync vsync = VSync::On;
    int fpsLimit = 0;                 // 0 = unlimited (vsync still applies)
    int outputBits = 8;               // 8 or 10 bits per channel swapchain
    bool highDpi = true;
    bool hdr = false;                 // reserved: HDR10 output is a later milestone

    // [aspect]
    AspectRatio aspect = AspectRatio::Window;
    HudLayout hud = HudLayout::Anchored;
    float safeArea = 0.0f;            // HUD inset, fraction of the content height

    // [render]
    GsRenderer gsRenderer = GsRenderer::Native;
    QualityMode quality = QualityMode::Native;
    int renderScale = 100;            // percent of the output resolution, used by quality = custom
    Upscaler upscaler = Upscaler::Fsr1;
    float sharpness = 0.2f;           // RCAS strength 0..1 (0 = off)
    AntiAliasing aa = AntiAliasing::Taa;
    int msaa = 1;                     // 1, 2, 4 or 8 samples

    // [texture]
    TextureFilter textureFilter = TextureFilter::Trilinear;
    int anisotropy = 16;              // 1 = off; clamped to the device limit
    float lodBias = 0.0f;             // added to the automatic upscaler bias
    bool autoLodBias = true;
    TextureFilter uiFilter = TextureFilter::Bilinear;  // 2D layer: point keeps pixel art crisp

    // [effects]
    EffectQuality shadows = EffectQuality::High;
    EffectQuality reflections = EffectQuality::High;
    EffectQuality bloom = EffectQuality::High;
    EffectQuality dof = EffectQuality::Off;
    EffectQuality motionBlur = EffectQuality::Off;
    EffectQuality heatHaze = EffectQuality::High;
    EffectQuality particles = EffectQuality::High;
    EffectQuality skyFog = EffectQuality::High;
    EffectQuality ambientOcclusion = EffectQuality::Medium;

    // [post]
    Tonemapper tonemapper = Tonemapper::AgXPunchy;
    float exposure = 0.0f;            // EV offset
    bool autoExposure = true;
    float bloomIntensity = 0.05f;
    float filmGrain = 0.0f;
    float vignette = 0.0f;
    float chromaticAberration = 0.0f;
    bool dither = true;
    float shutter = 0.5f;             // motion blur exposure, fraction of a frame (0.5 = 180 degrees)
    float focusDistance = 0.0f;       // depth of field focus in metres; 0 = autofocus on the centre
    float aperture = 1.4f;            // depth of field f-number (lens focal length comes from the field of view)

    // [graphics]
    Preset preset = Preset::High;

    // [gameplay] (sent to the game; not part of the graphics presets)
    AiCatchUp aiCatchUp = AiCatchUp::Original;

    // [scene] (test scene only; not saved)
    bool animate = true;
    float fov = 50.0f;                // vertical field of view in degrees (Hor+)

    // [debug]
    bool overlayText = true;          // the on-screen settings readout
};

// Resolved render scale (output / render) for the quality mode; custom uses renderScale.
float upscaleRatio(const Settings& settings);
const char* name(QualityMode);
const char* name(Upscaler);
const char* name(AntiAliasing);
const char* name(Preset);
const char* name(AspectRatio);
const char* name(WindowMode);
const char* name(VSync);
const char* name(Tonemapper);
const char* name(EffectQuality);
const char* name(TextureFilter);
const char* name(HudLayout);
const char* name(AiCatchUp);

// Content aspect ratio (width / height), or 0 for "match the window".
float aspectValue(AspectRatio ratio);

void applyPreset(Settings& settings, Preset preset);
// The preset whose values all match, or Custom.
Preset detectPreset(const Settings& settings);

// Sets one option from text ("render.aa", "taa"). Returns an empty string on success or an error message.
// Setting a preset member switches graphics.preset to custom; setting graphics.preset applies a preset.
std::string setOption(Settings& settings, std::string_view key, std::string_view value);
std::string getOption(const Settings& settings, std::string_view key);
// Every option: key, current value, allowed values and help, in file order.
struct OptionInfo { std::string key, value, allowed, help; bool persistent; };
std::vector<OptionInfo> describe(const Settings& settings);

// INI file I/O. load() returns warnings (unknown keys, bad values) and keeps defaults for those.
std::vector<std::string> loadSettings(Settings& settings, const std::string& path);
void saveSettings(const Settings& settings, const std::string& path);
std::string formatSettings(const Settings& settings);

}  // namespace gt4
