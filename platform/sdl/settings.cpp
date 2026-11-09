#include "settings.h"
#include "settings_table.h"

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

using namespace settings_table;

const std::vector<Option>& options() {
    static const std::vector<Option> table = [] {
        std::vector<Option> o;
        displayOptions(o);    // settings_display.cpp
        renderOptions(o);     // settings_render.cpp
        sceneOptions(o);      // settings_scene.cpp
        textureOptions(o);    // settings_render.cpp
        effectsOptions(o);    // settings_effects.cpp
        lightingOptions(o);   // settings_lighting.cpp
        inputOptions(o);      // settings_input.cpp
        audioOptions(o);      // settings_audio.cpp
        uiOptions(o);         // settings_ui.cpp
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
    // render.scene: original in the original preset; low to ultra take hybrid when it lands (PLAN.md B8,
    // ARCHITECTURE.md section 7) and leave it as it is until then. lighting.night likewise: original here, and the
    // quality presets leave it alone until N3 lands (PLAN.md F2).
    // input.response: original (the PS2's sticks, as before) in the original preset, modern in the others (PLAN.md
    // D12); its input.steer.* and input.pedal.* values are the player's, in no preset. input.haptics and
    // input.trigger_effects: original and off in the original preset (GT4's own rumble, as on the PS2), physics and on in
    // the others (D14); input.haptics.strength is the player's.
    static const PresetTable original{
        {"render.scene", "original"}, {"lighting.night", "original"}, {"render.gs", "original"}, {"display.field_mode", "blend"}, {"render.quality", "native"}, {"render.upscaler", "bilinear"}, {"render.sharpness", "0"},
        {"render.aa", "off"}, {"render.msaa", "1"}, {"texture.filter", "bilinear"}, {"texture.anisotropy", "1"},
        {"texture.lod_bias", "0"}, {"effects.shadows", "original"}, {"effects.reflections", "original"},
        {"effects.bloom", "original"}, {"effects.dof", "original"}, {"effects.motion_blur", "original"},
        {"effects.heat_haze", "original"}, {"effects.particles", "original"}, {"effects.sky_fog", "original"},
        {"effects.ambient_occlusion", "original"}, {"effects.model_detail", "original"}, {"display.race_frames", "fields"},
        {"post.tonemapper", "original"}, {"post.auto_exposure", "off"},
        {"post.exposure", "0"}, {"post.film_grain", "0"}, {"post.vignette", "0"},
        {"post.chromatic_aberration", "0"}, {"post.dither", "off"}, {"input.response", "original"}, {"input.haptics", "original"}, {"input.trigger_effects", "off"}};
    static const PresetTable low{
        {"render.gs", "native"}, {"display.field_mode", "blend"}, {"render.quality", "performance"}, {"render.upscaler", "fsr1"}, {"render.sharpness", "0.3"},
        {"render.aa", "fxaa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "4"},
        {"effects.shadows", "low"}, {"effects.reflections", "low"}, {"effects.bloom", "low"},
        {"effects.heat_haze", "low"}, {"effects.particles", "low"}, {"effects.sky_fog", "low"},
        {"effects.ambient_occlusion", "off"}, {"effects.model_detail", "original"}, {"display.race_frames", "progressive"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}, {"input.response", "modern"}, {"input.haptics", "physics"}, {"input.trigger_effects", "on"}};
    static const PresetTable medium{
        {"render.gs", "native"}, {"display.field_mode", "blend"}, {"render.quality", "quality"}, {"render.upscaler", "fsr2"}, {"render.sharpness", "0.25"},
        {"render.aa", "taa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "8"},
        {"effects.shadows", "medium"}, {"effects.reflections", "medium"}, {"effects.bloom", "medium"},
        {"effects.heat_haze", "medium"}, {"effects.particles", "medium"}, {"effects.sky_fog", "medium"},
        {"effects.ambient_occlusion", "low"}, {"effects.model_detail", "high"}, {"display.race_frames", "progressive"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}, {"input.response", "modern"}, {"input.haptics", "physics"}, {"input.trigger_effects", "on"}};
    static const PresetTable high{
        {"render.gs", "native"}, {"display.field_mode", "blend"}, {"render.quality", "native"}, {"render.upscaler", "fsr1"}, {"render.sharpness", "0.2"},
        {"render.aa", "taa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "16"},
        {"effects.shadows", "high"}, {"effects.reflections", "high"}, {"effects.bloom", "high"},
        {"effects.heat_haze", "high"}, {"effects.particles", "high"}, {"effects.sky_fog", "high"},
        {"effects.ambient_occlusion", "medium"}, {"effects.model_detail", "high"}, {"display.race_frames", "progressive"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}, {"input.response", "modern"}, {"input.haptics", "physics"}, {"input.trigger_effects", "on"}};
    static const PresetTable ultra{
        {"render.gs", "native"}, {"display.field_mode", "blend"}, {"render.quality", "native"}, {"render.upscaler", "fsr2"}, {"render.sharpness", "0.15"},
        {"render.aa", "taa"}, {"render.msaa", "1"}, {"texture.filter", "trilinear"}, {"texture.anisotropy", "16"},
        {"effects.shadows", "ultra"}, {"effects.reflections", "ultra"}, {"effects.bloom", "ultra"},
        {"effects.heat_haze", "ultra"}, {"effects.particles", "ultra"}, {"effects.sky_fog", "ultra"},
        {"effects.ambient_occlusion", "high"}, {"effects.model_detail", "ultra"}, {"display.race_frames", "progressive"}, {"post.tonemapper", "agx_punchy"}, {"post.auto_exposure", "on"}, {"input.response", "modern"}, {"input.haptics", "physics"}, {"input.trigger_effects", "on"}};
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

// What a preset sets besides its members (MENU_DESIGN 3.8): applyPreset sets these too, but detectPreset ignores them
// and changing one leaves graphics.preset as it is, since they are not graphics choices. ui.entry: hotkey in the
// original preset (GT4's screens untouched); hotkey in the others too until the host draws the pages (MENU_DESIGN M9
// sets integrated there): before, a hub opened from GT4's Options would be invisible. ui.pause likewise: gt4 in all
// of them until M9 sets native in the others (an invisible pause page would leave the race paused with no way out).
const PresetTable& presetFollowers(Preset preset) {
    static const PresetTable original{{"ui.entry", "hotkey"}, {"ui.pause", "gt4"}};
    static const PresetTable others{{"ui.entry", "hotkey"}, {"ui.pause", "gt4"}};
    static const PresetTable none;
    return preset == Preset::Original ? original : preset == Preset::Custom ? none : others;
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
const char* name(LightingMode v) { return kLighting[static_cast<size_t>(v)]; }
const char* name(MirrorDetail v) { return kMirror[static_cast<size_t>(v)]; }
const char* name(ModelDetail v) { return kModelDetail[static_cast<size_t>(v)]; }
const char* name(RaceFrames v) { return kRaceFrames[static_cast<size_t>(v)]; }
const char* name(TextureFilter v) { return kFilters[static_cast<size_t>(v)]; }
const char* name(HudLayout v) { return kHud[static_cast<size_t>(v)]; }
const char* name(AiCatchUp v) { return kCatchUp[static_cast<size_t>(v)]; }
const char* name(SceneRenderer v) { return kSceneRenderers[static_cast<size_t>(v)]; }
const char* name(FieldMode v) { return kFieldModes[static_cast<size_t>(v)]; }

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
    for (const PresetTable* table : {&presetValues(preset), &presetFollowers(preset)})
        for (const auto& [key, value] : *table) {
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

std::string unusedReason(const Settings& settings, std::string_view key) {
    if (!kHostDrawsPages && key == "ui.entry" && settings.uiEntry == UiEntry::Integrated)
        return "the host does not draw the settings pages yet (MENU_DESIGN M9): the hub GT4's Options open is invisible";
    if (!kHostDrawsPages && key == "ui.pause" && settings.uiPause == UiPause::Native)
        return "the host does not draw the settings pages yet (MENU_DESIGN M9): the native pause page is invisible";
    // (platform/render/pipeline.cpp: SceneSource::drawsOriginalEffects turns these off for the native GS's frames)
    if (settings.gsRenderer == GsRenderer::Native &&
        (key == "effects.bloom" || key == "post.bloom_intensity" || key == "post.tonemapper" || key == "post.auto_exposure"))
        return "not used with render.gs = native: the game's frames carry its own glare and colour grade";
    // (platform/audio/host_mixer: only the host's mixer interpolates other than the original)
    if ((key == "audio.interpolation" || key == "audio.spatial") && settings.audioMixer == AudioMixer::Game)
        return "not used with audio.mixer = game: the game mixes its own sound";
    return {};
}

std::vector<OptionInfo> describe(const Settings& settings) {
    std::vector<OptionInfo> result;
    for (const auto& option : options())
        result.push_back({option.key, option.get(settings), option.allowed, option.help, option.persistent,
                          unusedReason(settings, option.key)});
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
        out << "# " << option.allowed << "\n";
        if (const std::string unused = unusedReason(settings, key); !unused.empty()) out << "# (" << unused << ")\n";
        std::string value = option.get(settings);
        if (key == "render.scene" && (value == "parity" || value == "hybrid")) value = "original";   // (B7's parity, B8's
                                                                                                      // hybrid in check form: never saved)
        out << key.substr(dot + 1) << " = " << value << "\n";
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
    std::vector<std::string> seen;   // the options the file sets
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
        else seen.push_back(qualified);   // (a refused value is as if the line were missing)
    }
    settings.preset = detectPreset(settings);
    // A follower the file does not set (written before it existed) takes the loaded preset's value: an original
    // player's file from before ui.entry keeps GT4's screens untouched.
    for (const auto& [key, value] : presetFollowers(settings.preset))
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) find(key)->set(settings, value);
    // A page the host cannot draw yet is never opened from the file (a file written by a host from 44f286c to
    // 6ab90b7 holds integrated): --set still applies after the load.
    if (!kHostDrawsPages && settings.uiEntry == UiEntry::Integrated) {
        settings.uiEntry = UiEntry::Hotkey;
        warnings.push_back(path + ": ui.entry = integrated is not taken from the file until the host draws the "
                           "settings pages (MENU_DESIGN M9); hotkey instead (--set ui.entry=integrated still applies)");
    }
    if (!kHostDrawsPages && settings.uiPause == UiPause::Native) {
        settings.uiPause = UiPause::Gt4;
        warnings.push_back(path + ": ui.pause = native is not taken from the file until the host draws the settings "
                           "pages (MENU_DESIGN M9); gt4 instead (--set ui.pause=native still applies)");
    }
    return warnings;
}

}  // namespace gt4
