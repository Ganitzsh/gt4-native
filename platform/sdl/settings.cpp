#include "settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gt4 {
namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

bool parseInt(std::string_view text, int& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseFloat(std::string_view text, float& out) {
    // std::from_chars for floating point is not available on every supported standard library.
    std::string copy(text);
    if (copy.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) return false;
    out = static_cast<float>(value);
    return true;
}

bool parseBool(std::string_view text, bool& out) {
    const auto value = lower(text);
    if (value == "on" || value == "true" || value == "1" || value == "yes") { out = true; return true; }
    if (value == "off" || value == "false" || value == "0" || value == "no") { out = false; return true; }
    return false;
}

std::string formatFloat(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
    return buffer;
}

using Getter = std::function<std::string(const Settings&)>;
using Setter = std::function<std::string(Settings&, std::string_view)>;

struct Option {
    const char* key;
    const char* help;
    std::string allowed;
    bool persistent;
    Getter get;
    Setter set;
};

template <typename E>
Option enumOption(const char* key, E Settings::*member, std::vector<const char*> names, const char* help,
                  bool persistent = true) {
    std::string allowed;
    for (size_t i = 0; i < names.size(); ++i) allowed += (i ? " | " : "") + std::string(names[i]);
    return Option{key, help, allowed, persistent,
        [member, names](const Settings& s) { return std::string(names[static_cast<size_t>(s.*member)]); },
        [member, names, allowed](Settings& s, std::string_view text) -> std::string {
            const auto value = lower(text);
            for (size_t i = 0; i < names.size(); ++i)
                if (value == names[i]) { s.*member = static_cast<E>(i); return {}; }
            return "expected " + allowed;
        }};
}

Option intOption(const char* key, int Settings::*member, int minimum, int maximum, const char* help,
                 std::vector<int> choices = {}) {
    std::string allowed;
    if (choices.empty()) allowed = std::to_string(minimum) + ".." + std::to_string(maximum);
    else for (size_t i = 0; i < choices.size(); ++i) allowed += (i ? " | " : "") + std::to_string(choices[i]);
    return Option{key, help, allowed, true,
        [member](const Settings& s) { return std::to_string(s.*member); },
        [member, minimum, maximum, choices, allowed](Settings& s, std::string_view text) -> std::string {
            int value = 0;
            if (!parseInt(trim(text), value)) return "expected an integer, " + allowed;
            if (!choices.empty() && std::find(choices.begin(), choices.end(), value) == choices.end())
                return "expected " + allowed;
            if (value < minimum || value > maximum) return "expected " + allowed;
            s.*member = value;
            return {};
        }};
}

Option floatOption(const char* key, float Settings::*member, float minimum, float maximum, const char* help,
                   bool persistent = true) {
    const std::string allowed = formatFloat(minimum) + ".." + formatFloat(maximum);
    return Option{key, help, allowed, persistent,
        [member](const Settings& s) { return formatFloat(s.*member); },
        [member, minimum, maximum, allowed](Settings& s, std::string_view text) -> std::string {
            float value = 0.0f;
            if (!parseFloat(trim(text), value) || value < minimum || value > maximum) return "expected " + allowed;
            s.*member = value;
            return {};
        }};
}

Option boolOption(const char* key, bool Settings::*member, const char* help, bool persistent = true) {
    return Option{key, help, "on | off", persistent,
        [member](const Settings& s) { return std::string(s.*member ? "on" : "off"); },
        [member](Settings& s, std::string_view text) -> std::string {
            bool value = false;
            if (!parseBool(trim(text), value)) return "expected on | off";
            s.*member = value;
            return {};
        }};
}

Option stringOption(const char* key, std::string Settings::*member, const char* allowed, const char* help) {
    return Option{key, help, allowed, true,
        [member](const Settings& s) { return s.*member; },
        [member](Settings& s, std::string_view text) -> std::string {
            const auto value = trim(text);
            if (value.empty() || value.find_first_of("\r\n#;") != std::string_view::npos) return "expected a non-empty value";
            s.*member = std::string(value);
            return {};
        }};
}

const std::vector<const char*> kWindowModes{"windowed", "borderless", "exclusive"};
const std::vector<const char*> kVSync{"on", "off", "adaptive", "mailbox"};
const std::vector<const char*> kAspects{"window", "4:3", "16:9", "16:10", "21:9", "32:9"};
const std::vector<const char*> kHud{"anchored", "centered", "stretched"};
const std::vector<const char*> kQuality{"native", "ultra_quality", "quality", "balanced", "performance",
                                        "ultra_performance", "custom"};
const std::vector<const char*> kUpscalers{"bilinear", "bicubic", "lanczos", "fsr1", "fsr2"};
const std::vector<const char*> kGsRenderers{"original", "native"};
const std::vector<const char*> kAa{"off", "fxaa", "smaa", "smaa_t2x", "taa"};
const std::vector<const char*> kFilters{"point", "bilinear", "trilinear"};
const std::vector<const char*> kTonemappers{"original", "reinhard", "aces", "agx", "agx_punchy", "pbr_neutral"};
const std::vector<const char*> kEffects{"original", "off", "low", "medium", "high", "ultra"};
const std::vector<const char*> kPresets{"original", "low", "medium", "high", "ultra", "custom"};
const std::vector<const char*> kCatchUp{"original", "reduced", "off"};

const std::vector<Option>& options() {
    static const std::vector<Option> table = [] {
        std::vector<Option> o;
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
        o.push_back(intOption("display.output_bits", &Settings::outputBits, 8, 10,
            "Swapchain precision: 8 or 10 bits per channel (10 reduces banding where the display supports it).", {8, 10}));
        o.push_back(boolOption("display.high_dpi", &Settings::highDpi,
            "Render at the full pixel density of high-DPI displays (otherwise the OS scales the window)."));
        o.push_back(boolOption("display.hdr", &Settings::hdr,
            "Reserved for HDR10 output (not implemented yet; ignored)."));
        o.push_back(enumOption("aspect.ratio", &Settings::aspect, kAspects,
            "Content aspect ratio. Other window shapes get black bars (letterbox or pillarbox). 3D is Hor+: the "
            "vertical field of view stays fixed and wider ratios see more to the sides."));
        o.push_back(enumOption("aspect.hud", &Settings::hud, kHud,
            "2D layout: anchored = HUD elements keep to the edges of the content area and the menus use GT4's own "
            "widescreen layout, centered = 2D stays in a centred 4:3 area (some menu pages misalign), stretched = "
            "the 4:3 layout is stretched (the PS2 on a widescreen TV)."));
        o.push_back(floatOption("aspect.safe_area", &Settings::safeArea, 0.0f, 0.15f,
            "HUD inset from the content edges, as a fraction of the content height."));
        o.push_back(enumOption("render.gs", &Settings::gsRenderer, kGsRenderers,
            "The resolution the game's graphics are drawn at: original = the PS2's own (640 x 448, one pixel per "
            "PS2 pixel; `emulated` is accepted for it), native = the render resolution."));
        o.push_back(enumOption("render.quality", &Settings::quality, kQuality,
            "Render resolution relative to the output: native 1.0x, ultra_quality 1.3x, quality 1.5x, balanced "
            "1.7x, performance 2.0x, ultra_performance 3.0x smaller per axis; custom uses render.scale."));
        o.push_back(intOption("render.scale", &Settings::renderScale, 50, 200,
            "Render scale in percent of the output resolution when render.quality = custom. Above 100 is "
            "supersampling (downsampled with a Lanczos filter)."));
        o.push_back(enumOption("render.upscaler", &Settings::upscaler, kUpscalers,
            "Upscaler from render to output resolution: bilinear, bicubic (Catmull-Rom), lanczos (2 lobes), "
            "fsr1 (AMD FSR 1 EASU, spatial) or fsr2 (AMD FSR 2, temporal; it also does the anti-aliasing and at "
            "quality = native it is FSR Native AA)."));
        o.push_back(floatOption("render.sharpness", &Settings::sharpness, 0.0f, 1.0f,
            "Contrast-adaptive sharpening strength (AMD RCAS) after upscaling; 0 = off."));
        o.push_back(enumOption("render.aa", &Settings::aa, kAa,
            "Post-process anti-aliasing: fxaa, smaa (SMAA 1x), smaa_t2x (SMAA with 2-frame temporal "
            "supersampling) or taa (temporal AA; it also upsamples when rendering below the output). Ignored "
            "when render.upscaler = fsr2."));
        o.push_back(intOption("render.msaa", &Settings::msaa, 1, 8,
            "Multisample anti-aliasing samples for the 3D scene (clamped to the device).", {1, 2, 4, 8}));
        o.push_back(enumOption("texture.filter", &Settings::textureFilter, kFilters,
            "3D texture filtering: point, bilinear (nearest mip) or trilinear."));
        o.push_back(intOption("texture.anisotropy", &Settings::anisotropy, 1, 16,
            "Anisotropic filtering level for 3D textures (1 = off).", {1, 2, 4, 8, 16}));
        o.push_back(floatOption("texture.lod_bias", &Settings::lodBias, -3.0f, 3.0f,
            "Extra texture LOD bias added to the automatic one (negative = sharper, more shimmer)."));
        o.push_back(boolOption("texture.auto_lod_bias", &Settings::autoLodBias,
            "Bias texture LOD by log2(render / output width) when upscaling, so textures keep output-resolution "
            "detail; a further -1 with FSR 2 (AMD's recommendation) and -0.5 with TAA."));
        o.push_back(enumOption("texture.ui_filter", &Settings::uiFilter, kFilters,
            "Filtering of the 2D layer (HUD, menus, sprites): point keeps pixel art sharp."));
        o.push_back(enumOption("effects.shadows", &Settings::shadows, kEffects,
            "Shadow maps: low 1024 px 2x2 PCF, medium 2048 px 3x3 PCF, high 2048 px 16-tap Poisson, ultra 4096 px "
            "with contact-hardening (PCSS)."));
        o.push_back(enumOption("effects.reflections", &Settings::reflections, kEffects,
            "Environment reflections on cars: original = one sharp environment map; low..ultra = cube map of "
            "64..512 px prefiltered for roughness (GGX); off = a dimmed sharp map only."));
        o.push_back(enumOption("effects.bloom", &Settings::bloom, kEffects,
            "Bloom/glare: original = the PS2-style glare (bright pass over 3 levels, added); low..ultra = physically "
            "based bloom (energy-conserving mip chain, no threshold) over 4..8 levels."));
        o.push_back(enumOption("effects.dof", &Settings::dof, kEffects,
            "Depth of field: thin-lens circle of confusion with a golden-angle bokeh gather, 16..96 samples."));
        o.push_back(enumOption("effects.motion_blur", &Settings::motionBlur, kEffects,
            "Motion blur: per-pixel reconstruction filter with tile max velocity (McGuire et al. 2012), 8..24 samples."));
        o.push_back(enumOption("effects.heat_haze", &Settings::heatHaze, kEffects,
            "Heat haze (screen-space refraction over hot air and exhausts). Schema only: applied by the R1 game "
            "renderer; the test scene has none."));
        o.push_back(enumOption("effects.particles", &Settings::particles, kEffects,
            "Particles (smoke, sparks, dust): count and soft-particle depth fade. Schema only: applied by the R1 "
            "game renderer; the test scene has none."));
        o.push_back(enumOption("effects.sky_fog", &Settings::skyFog, kEffects,
            "Sky and fog: original = linear distance fog (the GS fog of the PS2); low = exponential height fog; "
            "medium..ultra = height fog with sun in-scattering."));
        o.push_back(enumOption("effects.ambient_occlusion", &Settings::ambientOcclusion, kEffects,
            "Ambient occlusion (an enhancement; the PS2 had none), planned as ground-truth AO (GTAO) with 4..16 "
            "directions. Schema only for now: not rendered yet."));
        o.push_back(enumOption("post.tonemapper", &Settings::tonemapper, kTonemappers,
            "HDR to display: original (clamp, as the PS2), reinhard (luminance), aces (Hill fit), agx and "
            "agx_punchy (Sobotka, Wrensch fit), pbr_neutral (Khronos PBR Neutral)."));
        o.push_back(floatOption("post.exposure", &Settings::exposure, -6.0f, 6.0f, "Exposure compensation in EV."));
        o.push_back(boolOption("post.auto_exposure", &Settings::autoExposure,
            "Automatic exposure from a luminance histogram, with smooth eye adaptation."));
        o.push_back(floatOption("post.bloom_intensity", &Settings::bloomIntensity, 0.0f, 1.0f,
            "Bloom mix (fraction of energy scattered by the lens)."));
        o.push_back(floatOption("post.film_grain", &Settings::filmGrain, 0.0f, 1.0f, "Film grain strength (0 = off)."));
        o.push_back(floatOption("post.vignette", &Settings::vignette, 0.0f, 1.0f, "Vignette strength (0 = off)."));
        o.push_back(floatOption("post.chromatic_aberration", &Settings::chromaticAberration, 0.0f, 1.0f,
            "Lateral chromatic aberration strength (0 = off)."));
        o.push_back(boolOption("post.dither", &Settings::dither,
            "Triangular-noise dither before quantising to the display format (removes banding)."));
        o.push_back(floatOption("post.shutter", &Settings::shutter, 0.0f, 1.0f,
            "Motion blur exposure time as a fraction of the frame (0.5 = a 180-degree shutter)."));
        o.push_back(floatOption("post.aperture", &Settings::aperture, 0.5f, 22.0f,
            "Depth of field f-number. The focal length follows from the field of view on a 24 mm tall sensor, so "
            "wide views have little blur and narrow (replay, telephoto) views a lot; smaller numbers blur more."));
        o.push_back(floatOption("post.focus_distance", &Settings::focusDistance, 0.0f, 10000.0f,
            "Depth of field focus distance in metres; 0 = autofocus on the centre of the screen."));
        o.push_back(enumOption("graphics.preset", &Settings::preset, kPresets,
            "Applies a set of the options above: original (PS2-faithful), low, medium, high, ultra. Changing "
            "any member afterwards makes it custom."));
        o.push_back(enumOption("gameplay.ai_catchup", &Settings::aiCatchUp, kCatchUp,
            "The AI's catch-up (GT4's slow car boost), in the events that set it: cars behind the player are "
            "sped up and cars ahead slowed down as the gaps grow. original = as on the PS2, reduced = half as "
            "strong, off = every car at its own pace."));
        o.push_back(boolOption("scene.animate", &Settings::animate, "Test scene: run the animation.", false));
        o.push_back(floatOption("scene.fov", &Settings::fov, 20.0f, 110.0f,
            "Test scene: vertical field of view in degrees (Hor+).", false));
        o.push_back(boolOption("debug.overlay", &Settings::overlayText, "On-screen settings readout.", false));
        return o;
    }();
    return table;
}

const Option* find(std::string_view key) {
    for (const auto& option : options())
        if (key == option.key) return &option;
    return nullptr;
}

using PresetTable = std::vector<std::pair<const char*, const char*>>;
const PresetTable& presetValues(Preset preset) {
    // Personal-taste effects (depth of field, motion blur, grain, vignette, aberration) belong only to the
    // PS2-faithful preset; the quality presets leave them as the player set them.
    static const PresetTable original{
        {"render.gs", "original"}, {"render.quality", "native"}, {"render.upscaler", "bilinear"}, {"render.sharpness", "0"},
        {"render.aa", "off"}, {"render.msaa", "1"}, {"texture.filter", "bilinear"}, {"texture.anisotropy", "1"},
        {"texture.lod_bias", "0"}, {"effects.shadows", "original"}, {"effects.reflections", "original"},
        {"effects.bloom", "original"}, {"effects.dof", "original"}, {"effects.motion_blur", "original"},
        {"effects.heat_haze", "original"}, {"effects.particles", "original"}, {"effects.sky_fog", "original"},
        {"effects.ambient_occlusion", "original"}, {"post.tonemapper", "original"}, {"post.auto_exposure", "off"},
        {"post.exposure", "0"}, {"post.film_grain", "0"}, {"post.vignette", "0"},
        {"post.chromatic_aberration", "0"}, {"post.dither", "off"}};
    static const PresetTable low{
        {"render.gs", "native"}, {"render.quality", "performance"}, {"render.upscaler", "fsr1"}, {"render.sharpness", "0.3"},
        {"render.aa", "fxaa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "4"},
        {"effects.shadows", "low"}, {"effects.reflections", "low"}, {"effects.bloom", "low"},
        {"effects.heat_haze", "low"}, {"effects.particles", "low"}, {"effects.sky_fog", "low"},
        {"effects.ambient_occlusion", "off"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}};
    static const PresetTable medium{
        {"render.gs", "native"}, {"render.quality", "quality"}, {"render.upscaler", "fsr2"}, {"render.sharpness", "0.25"},
        {"render.aa", "taa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "8"},
        {"effects.shadows", "medium"}, {"effects.reflections", "medium"}, {"effects.bloom", "medium"},
        {"effects.heat_haze", "medium"}, {"effects.particles", "medium"}, {"effects.sky_fog", "medium"},
        {"effects.ambient_occlusion", "low"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}};
    static const PresetTable high{
        {"render.gs", "native"}, {"render.quality", "native"}, {"render.upscaler", "fsr1"}, {"render.sharpness", "0.2"},
        {"render.aa", "taa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "16"},
        {"effects.shadows", "high"}, {"effects.reflections", "high"}, {"effects.bloom", "high"},
        {"effects.heat_haze", "high"}, {"effects.particles", "high"}, {"effects.sky_fog", "high"},
        {"effects.ambient_occlusion", "medium"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}};
    static const PresetTable ultra{
        {"render.gs", "native"}, {"render.quality", "native"}, {"render.upscaler", "fsr2"}, {"render.sharpness", "0.15"},
        {"render.aa", "taa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "16"},
        {"effects.shadows", "ultra"}, {"effects.reflections", "ultra"}, {"effects.bloom", "ultra"},
        {"effects.heat_haze", "ultra"}, {"effects.particles", "ultra"}, {"effects.sky_fog", "ultra"},
        {"effects.ambient_occlusion", "high"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}};
    static const PresetTable none;
    switch (preset) {
    case Preset::Original: return original;
    case Preset::Low: return low;
    case Preset::Medium: return medium;
    case Preset::High: return high;
    case Preset::Ultra: return ultra;
    case Preset::Custom: break;
    }
    return none;
}

bool isPresetMember(std::string_view key) {
    for (int p = 0; p < static_cast<int>(Preset::Custom); ++p)
        for (const auto& [member, value] : presetValues(static_cast<Preset>(p)))
            if (key == member) return true;
    return false;
}

std::string normalised(const Option& option, std::string_view value) {
    // Compare preset values through the option's own formatting (e.g. "0.30" == "0.3").
    Settings scratch;
    if (!option.set(scratch, value).empty()) return std::string(value);
    return option.get(scratch);
}

}  // namespace

float upscaleRatio(const Settings& s) {
    switch (s.quality) {
    case QualityMode::Native: return 1.0f;
    case QualityMode::UltraQuality: return 1.3f;
    case QualityMode::Quality: return 1.5f;
    case QualityMode::Balanced: return 1.7f;
    case QualityMode::Performance: return 2.0f;
    case QualityMode::UltraPerformance: return 3.0f;
    case QualityMode::Custom: break;
    }
    return 100.0f / static_cast<float>(std::clamp(s.renderScale, 50, 200));
}

const char* name(QualityMode v) { return kQuality[static_cast<size_t>(v)]; }
const char* name(Upscaler v) { return kUpscalers[static_cast<size_t>(v)]; }
const char* name(AntiAliasing v) { return kAa[static_cast<size_t>(v)]; }
const char* name(Preset v) { return kPresets[static_cast<size_t>(v)]; }
const char* name(AspectRatio v) { return kAspects[static_cast<size_t>(v)]; }
const char* name(WindowMode v) { return kWindowModes[static_cast<size_t>(v)]; }
const char* name(VSync v) { return kVSync[static_cast<size_t>(v)]; }
const char* name(Tonemapper v) { return kTonemappers[static_cast<size_t>(v)]; }
const char* name(EffectQuality v) { return kEffects[static_cast<size_t>(v)]; }
const char* name(TextureFilter v) { return kFilters[static_cast<size_t>(v)]; }
const char* name(HudLayout v) { return kHud[static_cast<size_t>(v)]; }
const char* name(AiCatchUp v) { return kCatchUp[static_cast<size_t>(v)]; }

float aspectValue(AspectRatio ratio) {
    switch (ratio) {
    case AspectRatio::Window: return 0.0f;
    case AspectRatio::R4_3: return 4.0f / 3.0f;
    case AspectRatio::R16_9: return 16.0f / 9.0f;
    case AspectRatio::R16_10: return 16.0f / 10.0f;
    case AspectRatio::R21_9: return 64.0f / 27.0f;  // the "21:9" panels are 2560x1080 / 3440x1440 (43:18 ~ 64:27)
    case AspectRatio::R32_9: return 32.0f / 9.0f;
    }
    return 0.0f;
}

void applyPreset(Settings& settings, Preset preset) {
    for (const auto& [key, value] : presetValues(preset)) {
        const auto* option = find(key);
        if (!option || !option->set(settings, value).empty())
            throw std::logic_error(std::string("Invalid preset entry ") + key);
    }
    settings.preset = preset;
}

Preset detectPreset(const Settings& settings) {
    for (int p = 0; p < static_cast<int>(Preset::Custom); ++p) {
        bool matches = true;
        for (const auto& [key, value] : presetValues(static_cast<Preset>(p))) {
            const auto* option = find(key);
            if (!option || option->get(settings) != normalised(*option, value)) { matches = false; break; }
        }
        if (matches) return static_cast<Preset>(p);
    }
    return Preset::Custom;
}

std::string setOption(Settings& settings, std::string_view key, std::string_view value) {
    const auto* option = find(trim(key));
    if (!option) return "unknown option '" + std::string(key) + "' (gt4_host --list-options)";
    if (option->key == std::string_view("graphics.preset")) {
        Settings scratch = settings;
        if (auto error = option->set(scratch, trim(value)); !error.empty()) return std::string(option->key) + ": " + error;
        if (scratch.preset != Preset::Custom) applyPreset(settings, scratch.preset);
        else settings.preset = Preset::Custom;
        return {};
    }
    // render.gs = emulated (the GS emulator it once named, before the native renderer drew every preset).
    if (option->key == std::string_view("render.gs") && lower(trim(value)) == "emulated") value = "original";
    if (auto error = option->set(settings, trim(value)); !error.empty()) return std::string(option->key) + ": " + error;
    if (isPresetMember(option->key)) settings.preset = detectPreset(settings);
    return {};
}

std::string getOption(const Settings& settings, std::string_view key) {
    const auto* option = find(trim(key));
    return option ? option->get(settings) : std::string();
}

std::vector<OptionInfo> describe(const Settings& settings) {
    std::vector<OptionInfo> result;
    for (const auto& option : options())
        result.push_back({option.key, option.get(settings), option.allowed, option.help, option.persistent});
    return result;
}

std::string formatSettings(const Settings& settings) {
    std::ostringstream out;
    out << "# GT4 native host settings. Every option is described in docs/RENDERING.md and by\n"
           "# `gt4_host --list-options`. Command-line overrides: --set section.key=value.\n"
           "version = " << Settings::kVersion << "\n";
    std::string section;
    // The preset goes first so that the explicit values below it win when the file is read back.
    const auto* preset = find("graphics.preset");
    out << "\n[graphics]\n# " << preset->allowed << "\npreset = " << preset->get(settings) << "\n";
    for (const auto& option : options()) {
        if (!option.persistent || option.key == std::string_view("graphics.preset")) continue;
        const std::string key = option.key;
        const auto dot = key.find('.');
        const auto current = key.substr(0, dot);
        if (current != section) { section = current; out << "\n[" << section << "]\n"; }
        out << "# " << option.allowed << "\n" << key.substr(dot + 1) << " = " << option.get(settings) << "\n";
    }
    return out.str();
}

void saveSettings(const Settings& settings, const std::string& path) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot write settings: " + temporary);
        file << formatSettings(settings);
        if (!file) throw std::runtime_error("Cannot write settings: " + temporary);
    }
    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) != 0)
        throw std::runtime_error("Cannot replace settings file: " + path);
}

std::vector<std::string> loadSettings(Settings& settings, const std::string& path) {
    std::vector<std::string> warnings;
    std::ifstream file(path, std::ios::binary);
    if (!file) return warnings;
    std::string line, section;
    int number = 0;
    while (std::getline(file, line)) {
        ++number;
        auto text = trim(line);
        if (text.empty() || text.front() == '#' || text.front() == ';') continue;
        const auto where = path + ":" + std::to_string(number) + ": ";
        if (text.front() == '[') {
            if (text.back() != ']') { warnings.push_back(where + "malformed section"); continue; }
            section = lower(trim(text.substr(1, text.size() - 2)));
            continue;
        }
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) { warnings.push_back(where + "expected key = value"); continue; }
        const auto key = lower(trim(text.substr(0, equals)));
        const auto value = trim(text.substr(equals + 1));
        if (section.empty() && key == "version") {
            int version = 0;
            if (!parseInt(value, version)) warnings.push_back(where + "bad version");
            else if (version > Settings::kVersion)
                warnings.push_back(where + "written by a newer version (" + std::to_string(version) +
                                   "); unknown options are ignored");
            // Migrations from older versions go here when the format changes.
            continue;
        }
        const auto qualified = section + "." + key;
        const auto* option = find(qualified);
        if (!option || !option->persistent) { warnings.push_back(where + "unknown option " + qualified); continue; }
        if (auto error = setOption(settings, qualified, value); !error.empty()) warnings.push_back(where + error);
    }
    settings.preset = detectPreset(settings);
    return warnings;
}

}  // namespace gt4
