// gt4_host settings and display math: option parsing, presets, the settings file round trip, the aspect
// ratios and the Hor+ reverse-Z projection. No GPU or window needed.
#include "chord_guard.h"
#include "settings.h"
#include "settings_menu.h"
#include "temp_path.h"
#include "vecmath.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
bool near(float a, float b, float epsilon = 1e-4f) { return std::fabs(a - b) <= epsilon; }
}  // namespace

int main() {
    using namespace gt4;

    // The menu's Select + Start chord never reaches the game; a short Start alone does, after its release.
    {
        using ps2::PadSelect;
        using ps2::PadStart;
        auto poll = [](ChordGuard& g, bool start, bool select, uint64_t t) {
            ps2::PadInput in{};
            in.button[PadStart] = start ? 1.0f : 0.0f;
            in.button[PadSelect] = select ? 1.0f : 0.0f;
            g.filter(in, t);
            return (in.button[PadStart] > 0.0f ? 1 : 0) | (in.button[PadSelect] > 0.0f ? 2 : 0);
        };
        ChordGuard chord;   // Start first, then Select: the chord
        bool leaked = poll(chord, true, false, 0) != 0 || poll(chord, true, false, 50) != 0;
        leaked |= poll(chord, true, true, 80) != 0 || poll(chord, true, true, 400) != 0 || poll(chord, false, true, 450) != 0;
        leaked |= poll(chord, false, false, 500) != 0 || poll(chord, false, false, 520) != 0;
        check(!leaked, "Select + Start (Start first) sends neither to the game");
        ChordGuard tap;     // a short Start alone: sent after its release, for a few polls
        const bool held = poll(tap, true, false, 1000) != 0 || poll(tap, true, false, 1100) != 0;
        int sent = 0;
        for (uint64_t t = 1150; t < 1300; t += 16) sent += poll(tap, false, false, t) == 1 ? 1 : 0;
        check(!held && sent == ChordGuard::kTapFrames, "a short Start reaches the game after its release");
        ChordGuard hold;    // a long Start alone: sent from kChordMs on
        check(poll(hold, true, false, 0) == 0 && poll(hold, true, false, 250) == 1, "a Start held alone reaches the game");
    }

    // Parsing and validation.
    Settings s;
    check(setOption(s, "render.aa", "smaa").empty() && s.aa == AntiAliasing::Smaa, "enum option parses");
    check(setOption(s, "render.aa", "SMAA_T2X").empty() && s.aa == AntiAliasing::SmaaT2x, "enum option ignores case");
    check(!setOption(s, "render.aa", "dlss").empty(), "unknown enum value is rejected");
    check(!setOption(s, "render.msaa", "3").empty(), "msaa accepts only 1, 2, 4, 8");
    {   // render.gs = native: the display settings the game's frames don't use, or use as something else
        Settings g;
        applyPreset(g, Preset::Ultra);   // native GS, TAA, FSR 2, native quality
        check(g.gsRenderer == GsRenderer::Native, "ultra draws with the native GS");
        check(!unusedReason(g, "render.upscaler").empty(), "fsr2 is listed: it doesn't run on the game's frames");
        check(unusedReason(g, "render.aa").empty(), "taa is not listed: it runs, as the GS scene's own resolve");
        check(unusedReason(g, "render.msaa").empty(), "msaa 1 is not listed");
        check(setOption(g, "render.msaa", "4").empty() && !unusedReason(g, "render.msaa").empty(),
              "msaa 4 is listed with render.gs = native");
        check(setOption(g, "render.aa", "fxaa").empty() && !unusedReason(g, "render.aa").empty(),
              "fxaa is listed while upscaler = fsr2");
        check(setOption(g, "render.upscaler", "fsr1").empty() && unusedReason(g, "render.aa").empty(),
              "fxaa is not listed with fsr1");
        check(!unusedReason(g, "render.upscaler").empty(), "fsr1 at native quality is listed: nothing is upscaled");
        check(setOption(g, "render.quality", "performance").empty() && unusedReason(g, "render.upscaler").empty(),
              "fsr1 below native quality is not listed");
        check(setOption(g, "render.aa", "smaa_t2x").empty() && !unusedReason(g, "render.aa").empty(),
              "smaa_t2x is listed: it runs as SMAA 1x");
        Settings o = g;
        check(setOption(o, "render.gs", "original").empty() && unusedReason(o, "render.aa").empty() &&
              unusedReason(o, "render.msaa").empty(), "nothing of these is listed with render.gs = original");
        Settings b;
        applyPreset(b, Preset::Original);
        for (const char* key : {"render.aa", "render.upscaler", "render.msaa"})
            check(unusedReason(b, key).empty(), "the original preset lists none of them");
    }
    {   // audio.interpolation is the host mixer's: unused while the game mixes (audio.mixer = game, the default)
        Settings a;
        check(!unusedReason(a, "audio.interpolation").empty(), "audio.interpolation unused with audio.mixer = game");
        check(setOption(a, "audio.mixer", "host").empty() && unusedReason(a, "audio.interpolation").empty(),
              "audio.interpolation used with audio.mixer = host");
        check(setOption(a, "audio.interpolation", "sinc").empty() && a.audioInterpolation == AudioInterpolation::Sinc,
              "audio.interpolation parses");
    }
    {   // audio.spatial (PLAN.md D9): the host mixer's too, and original in every preset while the game mixes
        Settings a;
        check(a.audioSpatial == AudioSpatial::Original && !unusedReason(a, "audio.spatial").empty(),
              "audio.spatial is original and unused with audio.mixer = game");
        check(setOption(a, "audio.mixer", "host").empty() && unusedReason(a, "audio.spatial").empty(),
              "audio.spatial used with audio.mixer = host");
        check(setOption(a, "audio.spatial", "stereo").empty() && a.audioSpatial == AudioSpatial::Stereo,
              "audio.spatial parses");
        check(!setOption(a, "audio.spatial", "hrtf").empty(), "audio.spatial has no hrtf yet (D9 step 3)");
        for (Preset preset : {Preset::Original, Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) {
            Settings t;
            applyPreset(t, preset);
            check(t.audioSpatial == AudioSpatial::Original, "every preset leaves audio.spatial original");
        }
    }
    check(setOption(s, "render.msaa", "8").empty() && s.msaa == 8, "msaa 8");
    check(!setOption(s, "render.scale", "300").empty(), "render scale is limited to 200 %");
    check(!setOption(s, "render.sharpness", "nan").empty(), "NaN is rejected");
    check(setOption(s, "post.exposure", "-1.5").empty() && near(s.exposure, -1.5f), "float option");
    check(setOption(s, "display.gpu", "Radeon RX").empty() && s.gpu == "Radeon RX", "string option keeps spaces");
    check(!setOption(s, "display.gpu", "").empty(), "empty GPU name is rejected");
    check(!setOption(s, "no.such_option", "1").empty(), "unknown option is rejected");
    check(getOption(s, "render.msaa") == "8", "getOption formats integers");

    // Presets: applying one sets its members; changing a member makes the preset custom.
    Settings p;
    check(setOption(p, "graphics.preset", "low").empty() && p.preset == Preset::Low, "preset low applies");
    check(p.upscaler == Upscaler::Fsr1 && p.quality == QualityMode::Performance && p.aa == AntiAliasing::Fxaa,
          "preset low sets the upscaler, quality and AA");
    check(detectPreset(p) == Preset::Low, "preset low is detected");
    check(setOption(p, "render.aa", "smaa").empty() && p.preset == Preset::Custom, "changing a member makes it custom");
    check(setOption(p, "render.aa", "fxaa").empty() && p.preset == Preset::Low, "restoring the member finds the preset again");
    check(setOption(p, "post.vignette", "0.4").empty() && p.preset == Preset::Low, "personal-taste effects are not preset members");
    Settings original;
    applyPreset(original, Preset::Original);
    check(original.tonemapper == Tonemapper::Original && original.bloom == EffectQuality::Original &&
          original.shadows == EffectQuality::Original && original.aa == AntiAliasing::Off &&
          original.textureFilter == TextureFilter::Bilinear && original.anisotropy == 1,
          "the original preset is PS2-faithful");
    for (Preset preset : {Preset::Original, Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) {
        Settings t;
        applyPreset(t, preset);
        check(detectPreset(t) == preset, "every preset is detected after applying it");
    }

    // display.field_mode: the PS2's blend in every preset (clean bobs on moving content: in none for now).
    {
        check(original.fieldMode == FieldMode::Blend && getOption(original, "display.field_mode") == "blend",
              "the original preset blends the two fields as before");
        for (Preset preset : {Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) {
            Settings t;
            applyPreset(t, preset);
            check(t.fieldMode == FieldMode::Blend, "low to ultra blend interlaced screens too");
        }
        check(Settings{}.fieldMode == FieldMode::Blend, "a settings file without field_mode blends");
        Settings f;
        applyPreset(f, Preset::High);
        check(setOption(f, "display.field_mode", "clean").empty() && f.preset == Preset::Custom, "field_mode is a preset member");
        check(!setOption(f, "display.field_mode", "bob").empty(), "an unknown field mode is refused");
    }

    // ui.entry (settings_ui.cpp, MENU_DESIGN 3.8): every preset sets it (hotkey in all of them until M9 draws the
    // pages), but it decides no preset: changing it keeps graphics.preset, and the file round trip keeps the player's.
    {
        Settings u;
        check(u.uiEntry == UiEntry::Hotkey && getOption(u, "ui.entry") == "hotkey", "ui.entry is hotkey by default (until M9)");
        applyPreset(u, Preset::Original);
        check(u.uiEntry == UiEntry::Hotkey && detectPreset(u) == Preset::Original, "the original preset opens by hotkey only");
        check(setOption(u, "ui.entry", "integrated").empty() && u.preset == Preset::Original && detectPreset(u) == Preset::Original,
              "ui.entry = integrated keeps the original preset");
        applyPreset(u, Preset::High);
        check(u.uiEntry == UiEntry::Hotkey, "a quality preset sets hotkey (until M9)");
        // Until M9 wires the host's page drawing, no preset may open a page the host cannot draw (MENU_DESIGN 3.8):
        // M9 changes this check to the table's values.
        for (Preset preset : {Preset::Original, Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) {
            Settings t;
            t.uiEntry = UiEntry::Integrated;
            t.uiPause = UiPause::Native;
            applyPreset(t, preset);
            check(t.uiEntry == UiEntry::Hotkey, "no preset sets ui.entry = integrated before M9");
            check(t.uiPause == UiPause::Gt4, "no preset sets ui.pause = native before M9");
        }
        check(setOption(u, "ui.entry", "integrated").empty() && u.preset == Preset::High && detectPreset(u) == Preset::High,
              "ui.entry = integrated keeps the high preset");
        check(!setOption(u, "ui.entry", "start").empty(), "an unknown ui.entry is refused");
        const auto path = gt4::test::uniqueTempPath("gt4_ui_entry", ".ini");
        saveSettings(u, path.string());
        Settings back;
        check(getOption(u, "ui.entry") == "integrated" && !unusedReason(u, "ui.entry").empty(),
              "integrated (from --set) is taken, and listed as unused until M9");
        const auto loaded = loadSettings(back, path.string());
        check(loaded.size() == 1 && back.uiEntry == UiEntry::Hotkey && back.preset == Preset::High,
              "integrated is not taken from the file until M9: hotkey, with one warning");
        check(unusedReason(back, "ui.entry").empty(), "hotkey is not listed as unused");
        std::filesystem::remove(path);
        // A file from before ui.entry: the loaded preset's value, whether the file names the preset or only matches it.
        auto loadText = [](const std::string& text) {
            const auto file = gt4::test::uniqueTempPath("gt4_ui_entry_old", ".ini");
            { std::ofstream out(file, std::ios::binary); out << text; }
            Settings s;
            const bool clean = loadSettings(s, file.string()).empty();
            std::filesystem::remove(file);
            return std::make_pair(clean, s);
        };
        auto [clean1, old1] = loadText("version = 1\n[graphics]\npreset = original\n");
        check(clean1 && old1.preset == Preset::Original && old1.uiEntry == UiEntry::Hotkey,
              "an original file without ui.entry opens by hotkey only");
        Settings originalValues;
        applyPreset(originalValues, Preset::Original);
        originalValues.preset = Preset::Custom;
        std::string custom = formatSettings(originalValues);
        custom = custom.substr(0, custom.find("\n[ui]")) + "\n";
        auto [clean2, old2] = loadText(custom);
        check(clean2 && old2.preset == Preset::Original && old2.uiEntry == UiEntry::Hotkey,
              "a file whose values are the original preset's, without ui.entry, opens by hotkey only");
        auto [clean3, old3] = loadText("version = 1\n[graphics]\npreset = original\n[ui]\nentry = integrated\n");
        check(!clean3 && old3.preset == Preset::Original && old3.uiEntry == UiEntry::Hotkey,
              "an original file that sets ui.entry = integrated loads hotkey, with a warning, until M9");
        auto [clean4, old4] = loadText("version = 1\n[graphics]\npreset = high\n");
        check(clean4 && old4.uiEntry == UiEntry::Hotkey, "a high file without ui.entry is hotkey (until M9)");
        auto [clean5, old5] = loadText("version = 1\n[graphics]\npreset = original\n[ui]\nentry = bogus\n");
        check(!clean5 && old5.uiEntry == UiEntry::Hotkey, "a refused ui.entry is as if missing: the original preset's hotkey");
    }

    // ui.pause (settings_ui.cpp, MENU_DESIGN 3.8, M7): as ui.entry, gt4 in every preset until M9 draws the pages.
    {
        Settings u;
        check(u.uiPause == UiPause::Gt4 && getOption(u, "ui.pause") == "gt4", "ui.pause is gt4 by default (until M9)");
        check(unusedReason(u, "ui.pause").empty(), "gt4 is not listed as unused");
        applyPreset(u, Preset::High);
        check(u.uiPause == UiPause::Gt4, "a quality preset sets gt4 (until M9)");
        check(setOption(u, "ui.pause", "native").empty() && u.preset == Preset::High && detectPreset(u) == Preset::High,
              "ui.pause = native keeps the high preset");
        check(!unusedReason(u, "ui.pause").empty(), "native (from --set) is taken, and listed as unused until M9");
        check(!setOption(u, "ui.pause", "strobe").empty(), "an unknown ui.pause is refused");
        const auto path = gt4::test::uniqueTempPath("gt4_ui_pause", ".ini");
        saveSettings(u, path.string());
        Settings back;
        const auto loaded = loadSettings(back, path.string());
        check(loaded.size() == 1 && back.uiPause == UiPause::Gt4 && back.preset == Preset::High,
              "native is not taken from the file until M9: gt4, with one warning");
        std::filesystem::remove(path);
        Settings original;
        original.uiPause = UiPause::Native;
        applyPreset(original, Preset::Original);
        check(original.uiPause == UiPause::Gt4 && detectPreset(original) == Preset::Original, "the original preset keeps GT4's pause");
        const auto bogus = gt4::test::uniqueTempPath("gt4_ui_pause_bogus", ".ini");
        { std::ofstream out(bogus, std::ios::binary); out << "version = 1\n[graphics]\npreset = original\n[ui]\npause = bogus\n"; }
        Settings refused;
        refused.uiPause = UiPause::Native;
        const bool refusedClean = loadSettings(refused, bogus.string()).empty();
        std::filesystem::remove(bogus);
        check(!refusedClean && refused.uiPause == UiPause::Gt4, "a refused ui.pause is as if missing: the original preset's gt4");
    }

    // lighting.night (settings_lighting.cpp, PLAN.md F2): original by default and in the original preset, which
    // turns it back off; the quality presets leave it as the player set it (in none of them until N3).
    {
        Settings n;
        check(n.night == LightingMode::Original && getOption(n, "lighting.night") == "original", "lighting.night is original by default");
        applyPreset(n, Preset::Original);
        check(setOption(n, "lighting.night", "enhanced").empty() && n.night == LightingMode::Enhanced, "lighting.night = enhanced is taken");
        check(detectPreset(n) == Preset::Custom, "the original preset with the night relit reads as custom");
        applyPreset(n, Preset::Original);
        check(n.night == LightingMode::Original && detectPreset(n) == Preset::Original, "the original preset turns the night relight off");
        check(setOption(n, "lighting.night", "enhanced").empty(), "enhanced again");
        applyPreset(n, Preset::High);
        check(n.night == LightingMode::Enhanced && detectPreset(n) == Preset::High, "a quality preset leaves it as set");
        for (Preset preset : {Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) {
            Settings t;
            applyPreset(t, preset);
            check(t.night == LightingMode::Original, "no preset turns the night relight on");
            check(!sceneSplitWanted(t), "no preset turns the scene split on");
        }
        // the scene split (sceneSplitWanted, F2b): with the night relight, beside the records it needs
        Settings split;
        check(!sceneSplitWanted(split) && !sceneSplitWanted(original), "no scene split by default or in the original preset");
        split.night = LightingMode::Enhanced;
        check(sceneRecordsWanted(split) && sceneSplitWanted(split), "the night relight wants the records and the split");
        split.night = LightingMode::Original;
        split.scene = SceneRenderer::Parity;
        check(sceneRecordsWanted(split) && sceneSplitWanted(split), "parity wants the records and the split (B7 draws views by B6's ids)");
        split.scene = SceneRenderer::Hybrid;
        check(sceneSplitWanted(split), "hybrid wants the split");
    }

    // render.scene (settings_scene.cpp): the GS path, or B7's parity check, until the modern renderer draws a view.
    {
        Settings r;
        check(r.scene == SceneRenderer::Original && getOption(r, "render.scene") == "original", "render.scene is original by default");
        check(original.scene == SceneRenderer::Original, "the original preset draws scenes on the GS path");
        check(setOption(r, "render.scene", "original").empty(), "render.scene = original is taken");
        {
            const std::string error = setOption(r, "render.scene", "modern");
            check(error.find("modern renderer") != std::string::npos && r.scene == SceneRenderer::Original,
                  "modern is refused until the modern renderer lands, with the reason");
        }
        check(setOption(r, "render.scene", "hybrid").empty() && r.scene == SceneRenderer::Hybrid &&
                  formatSettings(r).find("scene = hybrid") == std::string::npos,
              "render.scene = hybrid is taken (B8), never saved");
        check(setOption(r, "render.scene", "parity").empty() && r.scene == SceneRenderer::Parity, "render.scene = parity is taken (B7)");
        check(formatSettings(r).find("\nscene = original\n") != std::string::npos && formatSettings(r).find("scene = parity") == std::string::npos,
              "parity is a session's check: never saved (written as original)");
        check(setOption(r, "render.scene", "original").empty() && r.scene == SceneRenderer::Original, "and back to original");
        check(setOption(r, "render.scene", "bogus") == "render.scene: expected original, parity or hybrid", "a bad value names the ones taken");
        check(formatSettings(r).find("[render]") < formatSettings(r).find("\nscene = original\n") &&
                  formatSettings(r).find("\nscene = original\n") < formatSettings(r).find("[texture]"),
              "render.scene is written in the [render] section");
    }

    // Quality modes: render scale per axis (AMD's FSR ratios).
    Settings q;
    const std::pair<const char*, float> ratios[] = {{"native", 1.0f}, {"ultra_quality", 1.3f}, {"quality", 1.5f},
                                                    {"balanced", 1.7f}, {"performance", 2.0f}, {"ultra_performance", 3.0f}};
    for (const auto& [mode, ratio] : ratios) {
        setOption(q, "render.quality", mode);
        check(near(upscaleRatio(q), ratio), "quality mode ratio");
    }
    setOption(q, "render.quality", "custom");
    setOption(q, "render.scale", "200");
    check(near(upscaleRatio(q), 0.5f), "render scale 200 % is 2x supersampling");

    // File round trip, unknown keys and newer versions.
    const auto path = gt4::test::uniqueTempPath("gt4_host_settings_test", ".ini").string();
    Settings saved;
    applyPreset(saved, Preset::Ultra);
    setOption(saved, "render.sharpness", "0.35");
    setOption(saved, "aspect.ratio", "21:9");
    setOption(saved, "display.mode", "borderless");
    setOption(saved, "display.gpu", "1");
    saveSettings(saved, path);
    Settings loaded;
    const auto warnings = loadSettings(loaded, path);
    check(warnings.empty(), "a saved file loads without warnings");
    check(formatSettings(loaded) == formatSettings(saved), "every persistent option survives the round trip");
    check(loaded.preset == Preset::Custom && loaded.aspect == AspectRatio::R21_9 && loaded.windowMode == WindowMode::Borderless,
          "loaded values");
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version = 99\n[render]\naa = fxaa\nbogus = 1\n[texture]\nanisotropy = 7\n";
    }
    Settings future;
    const auto futureWarnings = loadSettings(future, path);
    check(future.aa == AntiAliasing::Fxaa, "known options of a newer file are read");
    check(futureWarnings.size() == 3, "newer version, unknown key and bad value are reported");
    check(future.anisotropy == Settings{}.anisotropy, "a bad value keeps the default");
    // The file as written, read back and written again: the same bytes (each module's options in the table's order).
    {
        saveSettings(saved, path);
        std::ifstream first(path, std::ios::binary);
        const std::string a{std::istreambuf_iterator<char>(first), std::istreambuf_iterator<char>()};
        first.close();
        Settings again;
        check(loadSettings(again, path).empty(), "the saved file reads back without warnings");
        saveSettings(again, path);
        std::ifstream second(path, std::ios::binary);
        const std::string b{std::istreambuf_iterator<char>(second), std::istreambuf_iterator<char>()};
        check(!a.empty() && a == b, "a saved file read back and saved again is byte for byte the same");
    }
    std::filesystem::remove(path);
    Settings missing;
    check(loadSettings(missing, path).empty(), "a missing file is not an error");

    // Aspect ratios.
    check(aspectValue(AspectRatio::Window) == 0.0f, "match window");
    check(near(aspectValue(AspectRatio::R16_9), 16.0f / 9.0f) && near(aspectValue(AspectRatio::R16_10), 1.6f) &&
          near(aspectValue(AspectRatio::R4_3), 4.0f / 3.0f) && near(aspectValue(AspectRatio::R21_9), 64.0f / 27.0f),
          "aspect values");

    // Hor+ reverse-Z projection: the vertical field of view is fixed, the horizontal one follows the aspect.
    const float fov = 50.0f * kPi / 180.0f;
    const Mat4 narrow = perspectiveReverseZ(fov, 4.0f / 3.0f, 0.1f);
    const Mat4 wide = perspectiveReverseZ(fov, 64.0f / 27.0f, 0.1f);
    check(near(narrow.at(1, 1), wide.at(1, 1)), "vertical scale does not depend on the aspect");
    const float tanNarrow = 1.0f / narrow.at(0, 0), tanWide = 1.0f / wide.at(0, 0);
    check(near(tanWide / tanNarrow, (64.0f / 27.0f) / (4.0f / 3.0f), 1e-3f), "horizontal extent grows with the aspect");
    const Vec4 atNear = narrow * Vec4(0.0f, 0.0f, -0.1f, 1.0f);
    const Vec4 far = narrow * Vec4(0.0f, 0.0f, -1.0e6f, 1.0f);
    check(near(atNear.z / atNear.w, 1.0f), "reverse Z: depth 1 at the near plane");
    check(far.z / far.w >= 0.0f && far.z / far.w < 1e-6f, "reverse Z: depth towards 0 at infinity");

    // The in-game settings menu.
    {
        using Input = SettingsMenu::Input;
        Settings m;
        applyPreset(m, Preset::High);
        SettingsMenu menu(true);
        menu.setStartValues(m);
        menu.open();
        auto selectedLabel = [&] {
            for (const auto& row : menu.view(m).rows) if (row.selected) return row.label;
            return std::string();
        };
        check(selectedLabel() == "Preset", "the menu starts on the first option");
        check(menu.input(Input::Right, m).changed && m.preset == Preset::Ultra, "right moves the preset on");
        check(!menu.input(Input::Right, m).changed && m.preset == Preset::Ultra, "the last value stays (custom is never chosen)");
        check(menu.input(Input::Left, m).changed && m.preset == Preset::High, "left moves it back");
        menu.input(Input::Down, m);
        check(selectedLabel() == "Renderer", "down selects the next row");
        check(menu.input(Input::Left, m).changed && m.gsRenderer == GsRenderer::Original && m.preset == Preset::Custom,
              "the renderer changes, the preset becomes custom");
        menu.input(Input::Down, m);
        menu.input(Input::Down, m);
        check(selectedLabel() == "Render scale", "render scale row");
        check(menu.input(Input::Left, m).changed && m.quality == QualityMode::Custom && m.renderScale == 90,
              "the render scale steps from the quality's scale and sets custom");
        menu.input(Input::PageDown, m);
        check(selectedLabel() == "Window", "page down goes to the next section");
        menu.input(Input::PageUp, m);
        check(selectedLabel() == "Preset", "page up goes to the top of the previous section");
        for (int i = 0; i < 4; ++i) menu.input(Input::PageDown, m);   // display, gameplay, image, actions
        menu.input(Input::Up, m);
        menu.input(Input::PageDown, m);
        check(selectedLabel() == "Resume", "the actions come last");
        auto view = menu.view(m);
        check(!menu.restartPending(m), "nothing waits for a restart");
        Settings widened = m;
        setOption(widened, "aspect.ratio", "16:9");
        check(menu.restartPending(widened), "the aspect ratio waits for a restart");
        check(menu.view(widened).footer.find("starts again") != std::string::npos, "the footer says so");
        const auto quitRow = menu.input(Input::Down, m);
        check(!quitRow.changed && selectedLabel() == "Quit game", "quit row");
        const auto quit = menu.input(Input::Accept, m);
        check(quit.closed && quit.quit && !menu.isOpen(), "quit closes the menu and asks to quit");
        menu.open();
        check(menu.input(Input::Back, m).closed && !menu.isOpen(), "back closes the menu");
        (void)view;
    }

    // Race frames apply from the next race: a preset switched in the menu that changes them says so.
    {
        Settings m;
        applyPreset(m, Preset::Original);
        SettingsMenu menu(true);
        menu.setStartValues(m);
        menu.open(m);
        check(!menu.nextRacePending(m), "nothing waits for the next race");
        bool marked = false;
        for (const auto& row : menu.view(m).rows) marked = marked || (row.label == "Race frames" && row.nextRace);
        check(marked, "the race frames row is marked");
        check(menu.input(SettingsMenu::Input::Right, m).changed && m.raceFrames == RaceFrames::Progressive,
              "a preset from original changes the race frames");
        check(menu.nextRacePending(m), "the race frames wait for the next race");
        check(menu.view(m).footer.find("next race") != std::string::npos, "the footer says so");
        menu.close();
        menu.open(m);
        check(!menu.nextRacePending(m), "opened again: nothing pending");
    }

    if (failures == 0) std::puts("host settings: all checks passed");
    return failures == 0 ? 0 : 1;
}
