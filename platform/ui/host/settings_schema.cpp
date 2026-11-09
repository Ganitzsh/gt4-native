#include "settings_schema.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace gt4::ui {

namespace {

// One row of the layout table: a key's label, apply class (MENU_DESIGN 3.7), the step of a range and how it shows,
// value labels for choices ("value=Label|value=Label"; others are shown capitalised), and whether Left / Right show a
// pending value first (the preset: its card before it applies, 4.3). A row with no key is a section header.
struct RowSpec {
    const char *key, *label;
    ApplyClass apply = ApplyClass::Live;
    float step = 0.0f;
    bool percent = false;
    const char *unit = "";
    const char *labels = "";
    bool previewFirst = false;
};
struct PageSpec {
    const char *id, *title, *icon;
    bool livePreview;
    std::vector<RowSpec> rows;
};

constexpr ApplyClass kLive = ApplyClass::Live, kConfirm = ApplyClass::Confirm, kNextRace = ApplyClass::NextRace,
                     kRestart = ApplyClass::Restart;

RowSpec header(const char *label) { return RowSpec{nullptr, label}; }

// MENU_DESIGN 4.1's tabs, with the options that exist today; a tab with none yet (HUD, until E9) is left out.
const std::vector<PageSpec> &pages() {
    static const std::vector<PageSpec> table = {
        {"game", "Game", "tab.game", false, {
            header("Gameplay"),
            {"gameplay.ai_catchup", "AI catch-up", kLive, 0, false, "", "original=Original (GT4)|off=Off"},
        }},
        {"graphics", "Graphics", "tab.graphics", true, {
            {"graphics.preset", "Preset", kLive, 0, false, "",
             "original=Original|low=Low|medium=Medium|high=High|ultra=Ultra|custom=Custom", true},
            {"render.gs", "Renderer", kLive, 0, false, "", "original=Original (PS2)|native=Native"},
            {"render.scene", "Scene renderer", kLive},
            {"render.quality", "Render resolution", kLive},
            {"render.scale", "Render scale", kLive, 10, false, "%"},
            {"render.upscaler", "Upscaler", kLive, 0, false, "",
             "bilinear=Bilinear|bicubic=Bicubic|lanczos=Lanczos|fsr1=AMD FSR 1|fsr2=AMD FSR 2"},
            {"render.sharpness", "Sharpening", kLive, 0.05f, true},
            {"render.aa", "Anti-aliasing", kLive, 0, false, "",
             "off=Off|fxaa=FXAA|smaa=SMAA|smaa_t2x=SMAA T2x|taa=TAA"},
            {"render.msaa", "MSAA", kLive},
            {"texture.hd_pack", "HD textures", kLive},
            header("Textures"),
            {"texture.filter", "Texture filter", kLive},
            {"texture.anisotropy", "Anisotropic filtering", kLive},
            {"texture.lod_bias", "Texture LOD bias", kLive, 0.25f},
            {"texture.auto_lod_bias", "Automatic LOD bias", kLive},
            header("Lighting"),
            {"effects.lighting", "Lighting", kLive},
            {"lighting.night", "Night lighting", kLive},
            {"effects.ambient_occlusion", "Ambient occlusion", kLive},
            header("Effects"),
            {"effects.shadows", "Shadows", kLive},
            {"effects.reflections", "Reflections", kLive},
            {"effects.road_reflections", "Road reflections", kLive},
            {"effects.bloom", "Bloom", kLive},
            {"effects.dof", "Depth of field", kLive},
            {"effects.motion_blur", "Motion blur", kLive},
            {"effects.particles", "Particles", kLive},
            {"effects.sky_fog", "Sky and fog", kLive},
            {"effects.heat_haze", "Heat haze", kLive},
            header("Detail"),
            {"effects.mirror", "Rear-view mirror", kNextRace, 0, false, "", "original=Original|high=High detail"},
            {"effects.model_detail", "Model detail", kNextRace},
            {"display.race_frames", "Race frames", kNextRace, 0, false, "",
             "progressive=Whole frames (480p)|fields=Fields (480i)"},
            {"display.field_mode", "Field display", kLive},
        }},
        {"display", "Display", "tab.display", false, {
            {"display.mode", "Window mode", kConfirm},
            {"display.monitor", "Monitor", kConfirm},
            {"display.gpu", "Graphics card", kConfirm},
            {"display.refresh_rate", "Refresh rate", kConfirm, 1, false, " Hz"},
            {"display.vsync", "V-sync", kLive},
            {"display.fps_limit", "Frame rate limit", kLive, 1},
            {"display.frame_interpolation", "Frame interpolation", kLive},
            {"display.output_bits", "Output bit depth", kConfirm},
            header("HDR"),
            {"display.hdr", "HDR", kConfirm},
            {"display.hdr_paper_white", "Paper white", kLive, 10, false, " nits"},
            {"display.hdr_peak", "Peak brightness", kLive, 50, false, " nits"},
            header("Shape"),
            {"aspect.ratio", "Aspect ratio", kRestart},
            {"aspect.hud", "Menu layout", kRestart, 0, false, "", "anchored=Stretched (PS2)|centered=Centred 4:3"},
            {"aspect.safe_area", "Safe area", kLive, 0.01f, true},
            {"texture.ui_filter", "Menu and HUD filter", kLive},
        }},
        {"image", "Image", "tab.image", true, {
            {"post.tonemapper", "Tone mapping", kLive},
            {"post.exposure", "Exposure", kLive, 0.1f},
            {"post.auto_exposure", "Auto exposure", kLive},
            {"post.bloom_intensity", "Bloom intensity", kLive, 0.01f},
            {"post.film_grain", "Film grain", kLive, 0.05f, true},
            {"post.vignette", "Vignette", kLive, 0.05f, true},
            {"post.chromatic_aberration", "Chromatic aberration", kLive, 0.05f, true},
            {"post.dither", "Dither", kLive},
        }},
        {"controls", "Controls", "tab.controls", false, {
            {"ui.prompts", "Button prompts", kLive, 0, false, "",
             "auto=Controller in use|xbox=Xbox|playstation=PlayStation|keyboard=Keyboard"},
            {"input.response", "Steering and pedal response", kLive},
            {"input.trigger_travel", "Trigger travel", kLive},
            header("Steering"),
            {"input.steer.deadzone", "Deadzone", kLive, 1, false, "%"},
            {"input.steer.outer_deadzone", "Outer deadzone", kLive, 1, false, "%"},
            {"input.steer.linearity", "Linearity", kLive, 0.05f},
            {"input.steer.sensitivity", "Sensitivity", kLive, 0.05f},
            {"input.steer.saturation", "Saturation", kLive, 1, false, "%"},
            {"input.steer.smoothing", "Smoothing", kLive, 1, false, " ms"},
            {"input.steer.key_rate", "Keyboard steering rate", kLive, 10},
            {"input.steer.game_band", "Game's steering band", kLive, 1},
            header("Pedals"),
            {"input.pedal.deadzone", "Deadzone", kLive, 1, false, "%"},
            {"input.pedal.outer_deadzone", "Outer deadzone", kLive, 1, false, "%"},
            {"input.pedal.linearity", "Linearity", kLive, 0.05f},
            {"input.pedal.smoothing", "Smoothing", kLive, 1, false, " ms"},
            header("Haptics"),
            {"input.haptics", "Haptics", kLive},
            {"input.haptics.strength", "Strength", kLive, 5, false, "%"},
            {"input.haptics.understeer", "Understeer", kLive},
            {"input.haptics.lockup", "Wheel lock-up", kLive},
            {"input.haptics.wheelspin", "Wheelspin", kLive},
            {"input.haptics.kerbs", "Kerbs", kLive},
            {"input.trigger_effects", "Trigger effects", kLive},
            header("Wheel"),
            {"input.wheel.mode", "Wheel", kLive, 0, false, "", "pad=As a gamepad|dfp=Driving Force Pro (GT4's)"},
            {"input.wheel.ffb_strength", "Force feedback", kLive, 5, false, "%"},
        }},
        {"audio", "Audio", "tab.audio", false, {
            {"audio.mixer", "Mixer", kLive},
            {"audio.interpolation", "Interpolation", kLive},
            {"audio.spatial", "Spatial audio", kLive},
        }},
        {"accessibility", "Accessibility", "tab.accessibility", false, {
            {"ui.entry", "Settings entry", kLive, 0, false, "", "hotkey=Hotkey only|integrated=Also from GT4's menus"},
            {"ui.pause", "Race pause", kLive, 0, false, "", "gt4=GT4's pause|native=Native pause page"},
        }},
        {"system", "System", "tab.system", false, {
            {"debug.overlay", "Performance readout", kLive},
        }},
    };
    return table;
}

const RowSpec *findRow(const std::string &key) {
    for (const auto &page : pages())
        for (const auto &row : page.rows)
            if (row.key && key == row.key) return &row;
    return nullptr;
}

// "fsr1=AMD FSR 1|..." -> the label of `value`; else the value capitalised, underscores as spaces.
std::string valueLabel(const RowSpec &row, const std::string &value) {
    const std::string labels = row.labels;
    size_t at = 0;
    while (at < labels.size()) {
        size_t end = labels.find('|', at);
        if (end == std::string::npos) end = labels.size();
        const std::string pair = labels.substr(at, end - at);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.compare(0, eq, value) == 0 && eq == value.size()) return pair.substr(eq + 1);
        at = end + 1;
    }
    std::string text = value;
    for (char &c : text)
        if (c == '_') c = ' ';
    if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

std::vector<std::string> split(const std::string &text, const std::string &by) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(by, at);
        if (end == std::string::npos) end = text.size();
        out.push_back(text.substr(at, end - at));
        at = end + by.size();
    }
    return out;
}

// The value Square resets a row to: the current preset's; none under Custom. A key the preset does not set keeps its
// value, so its reset changes nothing (the page plays the disable sound).
std::string presetValue(const Settings &settings, const std::string &key) {
    if (settings.preset == Preset::Custom) return std::string();
    Settings preset = settings;
    applyPreset(preset, settings.preset);
    return getOption(preset, key);
}

std::map<std::string, OptionInfo> described(const Settings &settings) {
    std::map<std::string, OptionInfo> out;
    for (auto &info : describe(settings)) out[info.key] = info;
    return out;
}

bool pending(const RowSpec &row, const Settings &settings, const Settings &baseline) {
    return (row.apply == kNextRace || row.apply == kRestart) &&
           getOption(settings, row.key) != getOption(baseline, row.key);
}

std::vector<uint8_t> payload(uint32_t kind, uint32_t request, bool ok, const std::string &body) {
    return encodeSettings(kind, request, ok, body);
}

}  // namespace

const std::vector<HiddenOption> &hiddenOptions() {
    static const std::vector<HiddenOption> hidden = {
        {"display.window_width", "the window's size: set by resizing the window"},
        {"display.window_height", "the window's size: set by resizing the window"},
        {"display.fullscreen_width", "the resolution row comes with the monitor's list of modes (a follow-up)"},
        {"display.fullscreen_height", "the resolution row comes with the monitor's list of modes (a follow-up)"},
        {"display.high_dpi", "read once at start-up, before any window"},
        {"texture.pack_path", "a folder path, set in settings.ini"},
        {"scene.animate", "the test scene's camera only, not a game setting"},
        {"scene.fov", "the test scene's camera only, not a game setting"},
        {"post.shutter", "the test scene's camera only (motion blur and depth of field over the game use GT4's own)"},
        {"post.aperture", "the test scene's camera only (motion blur and depth of field over the game use GT4's own)"},
        {"post.focus_distance", "the test scene's camera only (motion blur and depth of field over the game use GT4's own)"},
    };
    return hidden;
}

bool applyClass(const std::string &key, ApplyClass &out) {
    const RowSpec *row = findRow(key);
    if (!row) return false;
    out = row->apply;
    return true;
}

Schema buildSchema(const Settings &settings) {
    const auto info = described(settings);
    Schema schema;
    for (const auto &spec : pages()) {
        Page page;
        page.id = spec.id;
        page.title = spec.title;
        page.icon = spec.icon;
        page.livePreview = spec.livePreview;
        for (const auto &r : spec.rows) {
            Row row;
            if (!r.key) {
                row.kind = RowKind::Header;
                row.label = r.label;
                page.rows.push_back(std::move(row));
                continue;
            }
            const auto found = info.find(r.key);
            if (found == info.end()) continue;   // not an option of this build
            const OptionInfo &o = found->second;
            row.key = r.key;
            row.label = r.label;
            row.help = o.help;
            row.apply = r.apply;
            row.previewFirst = r.previewFirst;
            row.value = o.value;
            row.unused = o.unused;
            row.presetValue = presetValue(settings, r.key);
            const size_t range = o.allowed.find("..");
            if (o.allowed.find(" | ") != std::string::npos || range == std::string::npos) {
                row.kind = RowKind::Choice;
                for (const auto &value : split(o.allowed, " | "))
                    if (!value.empty()) row.choices.push_back({value, valueLabel(r, value)});
            } else {
                row.kind = RowKind::Step;
                row.minimum = std::strtof(o.allowed.substr(0, range).c_str(), nullptr);
                row.maximum = std::strtof(o.allowed.substr(range + 2).c_str(), nullptr);
                row.step = r.step > 0.0f ? r.step : 1.0f;
                row.percent = r.percent;
                row.unit = r.unit;
            }
            page.rows.push_back(std::move(row));
        }
        schema.tabs.push_back(page.id);
        schema.pages.push_back(std::move(page));
    }
    return schema;
}

std::vector<ValueUpdate> buildValues(const Settings &settings, const Settings &baseline) {
    const auto info = described(settings);
    std::vector<ValueUpdate> out;
    for (const auto &spec : pages())
        for (const auto &r : spec.rows) {
            if (!r.key) continue;
            const auto found = info.find(r.key);
            if (found == info.end()) continue;
            out.push_back({r.key, found->second.value, found->second.unused, pending(r, settings, baseline)});
        }
    return out;
}

SettingsBridge::SettingsBridge(const Settings &baseline) : baseline_(baseline) {}

std::vector<uint8_t> SettingsBridge::changedValues(const Settings &settings) {
    std::vector<ValueUpdate> changed;
    for (auto &v : buildValues(settings, baseline_)) {
        auto &last = sent_[v.key];
        if (last.value == v.value && last.unused == v.unused && last.pending == v.pending && !last.key.empty())
            continue;
        last = v;
        changed.push_back(std::move(v));
    }
    if (changed.empty()) return {};
    return payload(wire::kValues, 0, true, encodeValues(changed));
}

std::vector<uint8_t> SettingsBridge::refresh(const Settings &settings) {
    if (sent_.empty()) return {};
    return changedValues(settings);
}

Settings SettingsBridge::savable(const Settings &settings) const {
    Settings out = settings;
    for (const auto &[key, previous] : unconfirmed_) setOption(out, key, previous);
    return out;
}

SettingsBridge::Reply SettingsBridge::handle(Settings &settings, const uint8_t *data, size_t size) {
    Reply reply;
    wire::SetHeader header{};
    std::string key, value;
    if (!decodeSet(data, size, header, key, value)) return reply;   // malformed: nothing to answer
    static const char *const kinds[] = {"set", "revert", "action", "schema"};
    const char *kind = header.kind < 4 ? kinds[header.kind] : "?";
    const auto result = [&](bool ok, const std::string &why) {
        reply.payloads.push_back(payload(wire::kResult, header.request, ok, why));
        reply.summary = std::string(kind) + " " + key + (value.empty() ? "" : " = " + value) + ": " +
                        (ok ? "ok" : "refused (" + why + ")");
    };
    switch (header.kind) {
    case wire::kOpenSchema: {
        sent_.clear();
        for (auto &v : buildValues(settings, baseline_)) sent_[v.key] = v;
        reply.payloads.push_back(payload(wire::kSchema, 0, true, encodeSchema(buildSchema(settings))));
        reply.summary = "schema sent (" + std::to_string(sent_.size()) + " rows)";
        return reply;
    }
    case wire::kAction:
        if (key == kConfirmKeep) {
            // The page kept a confirm-class value: it is saved now (MENU_DESIGN 3.7, the architect's rule).
            const bool had = unconfirmed_.erase(value) > 0;
            reply.save = had;
            result(had, had ? std::string() : "no confirm-class change of " + value + " is waiting");
            return reply;
        }
        result(false, "unknown action " + key);
        return reply;
    case wire::kSet:
    case wire::kRevert: {
        ApplyClass apply = kLive;
        if (!applyClass(key, apply)) {
            result(false, key + " is not on a settings page");
            return reply;
        }
        const std::string before = getOption(settings, key);
        const std::string error = setOption(settings, key, value);
        if (!error.empty()) {
            result(false, error);
            if (auto values = changedValues(settings); !values.empty()) reply.payloads.push_back(std::move(values));
            return reply;
        }
        if (header.kind == wire::kRevert) {
            unconfirmed_.erase(key);   // back to the value before: nothing to save (it never was)
        } else if (apply == kConfirm) {
            if (!unconfirmed_.count(key)) unconfirmed_[key] = before;   // saved once kept
        } else {
            reply.save = true;
        }
        result(true, std::string());
        if (auto values = changedValues(settings); !values.empty()) {
            reply.changed = true;   // a row's value moved (a preset moves its followers too)
            reply.payloads.push_back(std::move(values));
        }
        reply.changed = reply.changed || getOption(settings, key) != before;
        reply.summary += apply == kConfirm ? (header.kind == wire::kRevert ? ", reverted" : ", waits to be kept")
                                           : reply.save ? ", saved" : "";
        return reply;
    }
    default:
        result(false, "unknown request kind " + std::to_string(header.kind));
        return reply;
    }
}

}  // namespace gt4::ui
