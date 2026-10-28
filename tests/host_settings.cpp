// gt4_host settings and display math: option parsing, presets, the settings file round trip, the aspect
// ratios and the Hor+ reverse-Z projection. No GPU or window needed.
#include "settings.h"
#include "settings_menu.h"
#include "vecmath.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

    // Parsing and validation.
    Settings s;
    check(setOption(s, "render.aa", "smaa").empty() && s.aa == AntiAliasing::Smaa, "enum option parses");
    check(setOption(s, "render.aa", "SMAA_T2X").empty() && s.aa == AntiAliasing::SmaaT2x, "enum option ignores case");
    check(!setOption(s, "render.aa", "dlss").empty(), "unknown enum value is rejected");
    check(!setOption(s, "render.msaa", "3").empty(), "msaa accepts only 1, 2, 4, 8");
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
    const auto path = (std::filesystem::temp_directory_path() / "gt4_host_settings_test.ini").string();
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
        check(menu.input(Input::Left, m).changed && m.gsRenderer == GsRenderer::Emulated && m.preset == Preset::Custom,
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
        for (int i = 0; i < 3; ++i) menu.input(Input::PageDown, m);
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

    if (failures == 0) std::puts("host settings: all checks passed");
    return failures == 0 ? 0 : 1;
}
