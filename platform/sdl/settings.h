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
// What draws the game's 3D scenes (docs/modern/ARCHITECTURE.md 4.1): original = the GS path (the PS2's picture);
// parity, hybrid and modern come with the modern renderer (PLAN.md B7, B8, B15): until then only original is taken.
enum class SceneRenderer : uint8_t { Original, Parity, Hybrid, Modern };
enum class AntiAliasing : uint8_t { Off, Fxaa, Smaa, SmaaT2x, Taa };
enum class TextureFilter : uint8_t { Point, Bilinear, Trilinear };
enum class Tonemapper : uint8_t { Original, Reinhard, Aces, AgX, AgXPunchy, PbrNeutral };
// Original: what the PS2 game draws (with the R1 renderer, the game's own GS output for that effect).
enum class EffectQuality : uint8_t { Original, Off, Low, Medium, High, Ultra };
enum class LightingMode : uint8_t { Original, Enhanced };
enum class MirrorDetail : uint8_t { Original, High };
enum class ModelDetail : uint8_t { Original, High, Ultra };
enum class RaceFrames : uint8_t { Fields, Progressive };
// How the two fields of an interlaced screen (the menus) are shown: blend = half and half (the PS2's picture without
// the bob), clean = without the line a TV hides and without the doubled copy of what moves between them.
enum class FieldMode : uint8_t { Blend, Clean };
enum class Preset : uint8_t { Original, Low, Medium, High, Ultra, Custom };
// The AI's catch-up (GT4's "slow car boost": cars behind sped up, cars ahead slowed down, by event).
enum class AiCatchUp : uint8_t { Original, Reduced, Off };
// The pedals' pressure scale (input.trigger_travel, settings_input.cpp; docs/modern/spikes/D1-bindings.md).
enum class TriggerTravel : uint8_t { Game, Full };
// Who mixes the SPU2's voices (audio.mixer, settings_audio.cpp; PLAN.md D5).
enum class AudioMixer : uint8_t { Game, Host };
// The host mixer's interpolation between a voice's samples (audio.interpolation; platform/audio/spu Interpolation).
enum class AudioInterpolation : uint8_t { Original, Cubic, Sinc };
// How the host mixer places the cars' sounds (audio.spatial; platform/audio/host_mixer.h Spatial; PLAN.md D9).
enum class AudioSpatial : uint8_t { Original, Stereo, Hrtf };
// The sticks' and pedals' response (input.response, platform/input/response.h; PLAN.md D12).
enum class InputResponse : uint8_t { Original, Modern };
// Gamepad haptics (input.haptics, platform/input/haptics.h; PLAN.md D14): original = GT4's own rumble only (D2).
enum class HapticsMode : uint8_t { Original, Physics };
// A wheel (a device SDL types as one; PLAN.md D7): pad = a DualShock 2 through its profile, dfp = the Driving Force
// Pro GT4 sees through LGDEV, with GT4's own force effects.
enum class WheelMode : uint8_t { Pad, Dfp };
// How the settings menu opens (ui.entry, settings_ui.cpp; docs/modern/MENU_DESIGN.md 3.2, 3.8): hotkey = the host's
// keys only, integrated = GT4's own Options items too (the game's hooks, runtime/menu/menu_host.cpp).
enum class UiEntry : uint8_t { Hotkey, Integrated };
// The race pause (ui.pause, settings_ui.cpp; MENU_DESIGN 3.2 point 4, M7): gt4 = GT4's own pause.strb, native = the
// native pause page (the game's copy of RacePause::update, game_src/func_003C3820.cpp).
enum class UiPause : uint8_t { Gt4, Native };
// The pages' button prompts (ui.prompts, settings_ui.cpp; MENU_DESIGN M9): auto = the controller in use.
enum class UiPrompts : uint8_t { Auto, Xbox, PlayStation, Keyboard };

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
    bool frameInterpolation = true;   // the game's 60 Hz frames interpolated to the display's refresh (moving 3D)
    int outputBits = 8;               // 8 or 10 bits per channel swapchain
    bool highDpi = true;
    bool hdr = false;                 // HDR10 output (PQ, BT.2020) where the display offers it
    float hdrPaperWhite = 200.0f;     // HDR: the nits of SDR white
    float hdrPeak = 1000.0f;          // HDR: the display's peak, nits (highlights expand towards it)
    FieldMode fieldMode = FieldMode::Blend;   // display.field_mode (GsnScene::setFieldMode)

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
    SceneRenderer scene = SceneRenderer::Original;   // render.scene (settings_scene.cpp)
    LightingMode night = LightingMode::Original;     // lighting.night (settings_lighting.cpp; PLAN.md F2)

    // [texture]
    TextureFilter textureFilter = TextureFilter::Trilinear;
    int anisotropy = 16;              // 1 = off; clamped to the device limit
    float lodBias = 0.0f;             // added to the automatic upscaler bias
    bool autoLodBias = true;
    TextureFilter uiFilter = TextureFilter::Bilinear;  // 2D layer: point keeps pixel art crisp
    bool hdTextures = true;           // a texture pack's replacements (PCSX2 format), when one is installed
    std::string texturePack = "auto"; // its folder; auto: the usual places (platform/gsnative/texture_pack.h)
    std::string texturePackFound;     // set at start-up: the folder in use, empty when none (not saved)

    // [effects]
    EffectQuality shadows = EffectQuality::High;
    EffectQuality reflections = EffectQuality::High;
    bool roadReflections = false;     // screen-space reflections on the road: GT4's dry courses have none
    EffectQuality bloom = EffectQuality::High;
    EffectQuality dof = EffectQuality::Off;
    EffectQuality motionBlur = EffectQuality::Off;
    EffectQuality heatHaze = EffectQuality::High;
    EffectQuality particles = EffectQuality::High;
    EffectQuality skyFog = EffectQuality::High;
    EffectQuality ambientOcclusion = EffectQuality::Medium;
    LightingMode lighting = LightingMode::Original;   // the game's 3D scenes graded as a modern renderer's
    MirrorDetail mirror = MirrorDetail::High;         // the rear-view mirror's course, textures and cars (sent to the game)
    ModelDetail modelDetail = ModelDetail::High;      // models' detail levels kept 1x / 2x / 4x as far (sent to the game)
    RaceFrames raceFrames = RaceFrames::Progressive;  // races drawn as 224-line fields or whole 448-line frames (the game's 480p)

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

    // [audio] (settings_audio.cpp; not part of the graphics presets)
    AudioMixer audioMixer = AudioMixer::Game;   // sent to the game (kOptionHostMixer)
    AudioInterpolation audioInterpolation = AudioInterpolation::Original;   // the host mixer's (audio.mixer = host)
    AudioSpatial audioSpatial = AudioSpatial::Original;                     // the host mixer's (audio.mixer = host)

    // [ui] (settings_ui.cpp, lane M; set by every preset, but not part of what makes one: MENU_DESIGN 3.8)
    UiEntry uiEntry = UiEntry::Hotkey;   // sent to the game (kOptionUiEntry); integrated in low to ultra from M9
    UiPrompts uiPrompts = UiPrompts::Auto;   // sent to the game as kOptionPromptStyle (platform/ui/host/prompt_style.h)
    UiPause uiPause = UiPause::Gt4;      // sent to the game (kOptionUiPause); native in low to ultra from M9

    // [input] (settings_input.cpp; not part of the graphics presets but for input.response)
    TriggerTravel triggerTravel = TriggerTravel::Game;   // sent to the game (kOptionTriggerTravel)
    // (input.response is a graphics-preset member: original in the original preset, modern in the others)
    InputResponse inputResponse = InputResponse::Modern;
    float steerDeadzone = 5.0f;         // percent of travel (GT4's band is compensated: steerGameBand)
    float steerOuterDeadzone = 2.0f;    // percent
    float steerLinearity = 1.0f;        // exponent
    float steerSensitivity = 1.0f;      // gain
    float steerSaturation = 100.0f;     // percent of lock
    float steerSmoothing = 0.0f;        // ms
    float steerKeyRate = 150.0f;        // ms to full lock from a key
    float steerGameBand = 29.0f;        // percent of a side: GT4's steering band, compensated in modern (PLAN D12)
    float pedalDeadzone = 5.0f;         // percent
    float pedalOuterDeadzone = 2.0f;    // percent
    float pedalLinearity = 1.0f;        // exponent
    float pedalSmoothing = 0.0f;        // ms
    // (input.haptics and input.trigger_effects are graphics-preset members: original, off in the original preset)
    HapticsMode hapticsMode = HapticsMode::Physics;
    float hapticsStrength = 100.0f;     // percent
    bool triggerEffects = true;
    bool hapticsUndersteer = true, hapticsLockUp = true, hapticsWheelspin = true, hapticsKerbs = true;
    // A wheel (PLAN.md D7; not a preset member). pad until D7's force feedback lands (rule 10: off until finished),
    // then dfp, the PS2's behaviour.
    WheelMode wheelMode = WheelMode::Pad;
    float wheelFfbStrength = 100.0f;    // percent; never past platform/input/wheel_ffb.h's ceiling

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
const char* name(LightingMode);
const char* name(MirrorDetail);
const char* name(ModelDetail);
const char* name(RaceFrames);
const char* name(TextureFilter);
const char* name(HudLayout);
const char* name(AiCatchUp);
const char* name(SceneRenderer);
const char* name(FieldMode);
// Whether the game should send its scene records (runtime kOptionSceneRecords; docs/modern/CONTRACTS.md a.1):
// GT4_HOST_RECORD_SCENE=1, or a render.scene other than original (settings_scene.cpp, the one place that reads them).
bool sceneRecordsWanted(const Settings& settings);
// Whether the front end splits its draws by scene view (A10; B6's mesh and material ids exist only then): with scene
// records wanted, for a modern renderer (render.scene = hybrid or modern) or the night relight's course lamps
// (lighting.night = enhanced, PLAN.md F2b). The pixels are the same either way. GT4_GSN_SCENE_SPLIT=1 turns it on too
// (gs_host.cpp).
bool sceneSplitWanted(const Settings& settings);

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
// `unused`: why the option has no effect with the other values (empty when it has one).
struct OptionInfo { std::string key, value, allowed, help; bool persistent; std::string unused; };
std::vector<OptionInfo> describe(const Settings& settings);
// Why `key` does nothing with these settings, or empty: with render.gs = native the game's frames carry its own
// glare and colour grade, and the post chain adds no tone mapping, automatic exposure or bloom to them.
std::string unusedReason(const Settings& settings, std::string_view key);

// INI file I/O. load() returns warnings (unknown keys, bad values) and keeps defaults for those.
std::vector<std::string> loadSettings(Settings& settings, const std::string& path);
void saveSettings(const Settings& settings, const std::string& path);
std::string formatSettings(const Settings& settings);

}  // namespace gt4
