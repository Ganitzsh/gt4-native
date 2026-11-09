#pragma once
// The option table's parts (settings.cpp lists them; PLAN.md E1). Each module adds its options in its own file:
//   settings_display.cpp   display.*, aspect.*
//   settings_render.cpp    render.*, texture.*
//   settings_scene.cpp     the modern renderer's scene options (render.scene)
//   settings_effects.cpp   effects.*, display.race_frames, post.*
//   settings_lighting.cpp  lighting.* (night lighting, lane F)
//   settings_input.cpp     input.* (lane D)
//   settings_audio.cpp     audio.* (lane D)
//   settings_ui.cpp        ui.* (how Settings opens, lane M)
// The order of the parts and of the options in them is the order of the settings file: an option goes after the
// others of its section, in its module's part. Internal to the settings: not for other code.
#include "settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4::settings_table {

inline std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}

inline std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

inline bool parseInt(std::string_view text, int& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

inline bool parseFloat(std::string_view text, float& out) {
    // std::from_chars for floating point is not available on every supported standard library.
    std::string copy(text);
    if (copy.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) return false;
    out = static_cast<float>(value);
    return true;
}

inline bool parseBool(std::string_view text, bool& out) {
    const auto value = lower(text);
    if (value == "on" || value == "true" || value == "1" || value == "yes") { out = true; return true; }
    if (value == "off" || value == "false" || value == "0" || value == "no") { out = false; return true; }
    return false;
}

inline std::string formatFloat(float value) {
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

inline Option intOption(const char* key, int Settings::*member, int minimum, int maximum, const char* help,
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

inline Option floatOption(const char* key, float Settings::*member, float minimum, float maximum, const char* help,
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

inline Option boolOption(const char* key, bool Settings::*member, const char* help, bool persistent = true) {
    return Option{key, help, "on | off", persistent,
        [member](const Settings& s) { return std::string(s.*member ? "on" : "off"); },
        [member](Settings& s, std::string_view text) -> std::string {
            bool value = false;
            if (!parseBool(trim(text), value)) return "expected on | off";
            s.*member = value;
            return {};
        }};
}

inline Option stringOption(const char* key, std::string Settings::*member, const char* allowed, const char* help) {
    return Option{key, help, allowed, true,
        [member](const Settings& s) { return s.*member; },
        [member](Settings& s, std::string_view text) -> std::string {
            const auto value = trim(text);
            if (value.empty() || value.find_first_of("\r\n#;") != std::string_view::npos) return "expected a non-empty value";
            s.*member = std::string(value);
            return {};
        }};
}

// The names of each enum's values, in the enum's order (the file's spelling; name() in settings.cpp).
inline const std::vector<const char*> kWindowModes{"windowed", "borderless", "exclusive"};
inline const std::vector<const char*> kVSync{"on", "off", "adaptive", "mailbox"};
inline const std::vector<const char*> kAspects{"window", "4:3", "16:9", "16:10", "21:9", "32:9"};
inline const std::vector<const char*> kHud{"anchored", "centered", "stretched"};
inline const std::vector<const char*> kQuality{"native", "ultra_quality", "quality", "balanced", "performance",
                                               "ultra_performance", "custom"};
inline const std::vector<const char*> kUpscalers{"bilinear", "bicubic", "lanczos", "fsr1", "fsr2", "fsr3"};
inline const std::vector<const char*> kGsRenderers{"original", "native"};
inline const std::vector<const char*> kSceneRenderers{"original", "parity", "hybrid", "modern"};
inline const std::vector<const char*> kAa{"off", "fxaa", "smaa", "smaa_t2x", "taa"};
inline const std::vector<const char*> kFilters{"point", "bilinear", "trilinear"};
inline const std::vector<const char*> kTonemappers{"original", "reinhard", "aces", "agx", "agx_punchy", "pbr_neutral"};
inline const std::vector<const char*> kEffects{"original", "off", "low", "medium", "high", "ultra"};
inline const std::vector<const char*> kPresets{"original", "low", "medium", "high", "ultra", "custom"};
inline const std::vector<const char*> kCatchUp{"original", "reduced", "off"};
inline const std::vector<const char*> kLighting{"original", "enhanced"};
inline const std::vector<const char*> kMirror{"original", "high"};
inline const std::vector<const char*> kModelDetail{"original", "high", "ultra"};
inline const std::vector<const char*> kRaceFrames{"fields", "progressive"};
inline const std::vector<const char*> kFieldModes{"blend", "clean"};

// The parts, in the table's order (settings.cpp options()).
void displayOptions(std::vector<Option>& o);    // settings_display.cpp
void renderOptions(std::vector<Option>& o);     // settings_render.cpp
void sceneOptions(std::vector<Option>& o);      // settings_scene.cpp
void textureOptions(std::vector<Option>& o);    // settings_render.cpp
void effectsOptions(std::vector<Option>& o);    // settings_effects.cpp
void lightingOptions(std::vector<Option>& o);   // settings_lighting.cpp
void inputOptions(std::vector<Option>& o);      // settings_input.cpp
void audioOptions(std::vector<Option>& o);      // settings_audio.cpp
void uiOptions(std::vector<Option>& o);         // settings_ui.cpp

}  // namespace gt4::settings_table
