#include "settings_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace gt4 {

struct SettingsMenu::Item {
    enum Kind { Header, Choice, Step, Action };
    Kind kind = Header;
    std::string label, key, help;
    std::vector<std::pair<std::string, std::string>> choices;   // value, label
    float step = 0.0f, minimum = 0.0f, maximum = 0.0f;
    bool percent = false;     // a 0..1 value shown as a percentage
    bool restart = false;
    int action = 0;           // 1 resume, 2 quit
};

namespace {

using Item = SettingsMenu::Item;
using Choices = std::vector<std::pair<std::string, std::string>>;

Item header(std::string label) {
    Item item;
    item.label = std::move(label);
    return item;
}

Item choice(std::string label, std::string key, Choices choices, std::string help, bool restart = false) {
    Item item;
    item.kind = Item::Choice;
    item.label = std::move(label);
    item.key = std::move(key);
    item.choices = std::move(choices);
    item.help = std::move(help);
    item.restart = restart;
    return item;
}

Item step(std::string label, std::string key, float step, float minimum, float maximum, bool percent, std::string help) {
    Item item;
    item.kind = Item::Step;
    item.label = std::move(label);
    item.key = std::move(key);
    item.step = step;
    item.minimum = minimum;
    item.maximum = maximum;
    item.percent = percent;
    item.help = std::move(help);
    return item;
}

Item action(std::string label, int id, std::string help) {
    Item item;
    item.kind = Item::Action;
    item.label = std::move(label);
    item.action = id;
    item.help = std::move(help);
    return item;
}

const Choices kOnOff{{"on", "On"}, {"off", "Off"}};
const Choices kEffectLevels{{"original", "Original"}, {"off", "Off"}, {"low", "Low"}, {"medium", "Medium"},
                            {"high", "High"}, {"ultra", "Ultra"}};

std::vector<Item> buildItems(bool game) {
    std::vector<Item> items;
    items.push_back(header("GRAPHICS"));
    items.push_back(choice("Preset", "graphics.preset",
        {{"original", "Original (PS2)"}, {"low", "Low"}, {"medium", "Medium"}, {"high", "High"}, {"ultra", "Ultra"},
         {"custom", "Custom"}},
        "Original keeps the PS2 look: the emulated graphics chip at 640 x 448. The others draw the game on the "
        "GPU at your resolution."));
    if (game)
        items.push_back(choice("Renderer", "render.gs", {{"emulated", "PS2 (emulated GS)"}, {"native", "Native (GPU)"}},
            "PS2: the graphics chip emulated at its own resolution, the original image. Native: the same "
            "drawing done by the GPU at the render resolution."));
    items.push_back(choice("Render resolution", "render.quality",
        {{"native", "100%"}, {"ultra_quality", "77%"}, {"quality", "67%"}, {"balanced", "59%"},
         {"performance", "50%"}, {"ultra_performance", "33%"}, {"custom", "Custom"}},
        "Resolution of the image before upscaling, per axis, relative to the output."));
    items.push_back(step("Render scale", "render.scale", 10.0f, 50.0f, 200.0f, false,
        "Render resolution in percent of the output (sets the resolution to custom). Above 100% is "
        "supersampling."));
    items.push_back(choice("Upscaler", "render.upscaler",
        {{"bilinear", "Bilinear"}, {"bicubic", "Bicubic"}, {"lanczos", "Lanczos"}, {"fsr1", "AMD FSR 1"},
         {"fsr2", "AMD FSR 2"}},
        game ? "From the render to the output resolution. FSR 2 needs motion vectors, which the game's frames "
               "do not have yet: it runs as FSR 1."
             : "From the render to the output resolution. FSR 2 is temporal and also anti-aliases."));
    items.push_back(step("Sharpening", "render.sharpness", 0.05f, 0.0f, 1.0f, true,
        "AMD RCAS contrast-adaptive sharpening after upscaling."));
    items.push_back(choice("Anti-aliasing", "render.aa",
        {{"off", "Off"}, {"fxaa", "FXAA"}, {"smaa", "SMAA"}, {"smaa_t2x", "SMAA T2x"}, {"taa", "TAA"}},
        game ? "Post-process anti-aliasing. Without motion vectors (the game's frames), TAA and SMAA T2x run "
               "as SMAA."
             : "Post-process anti-aliasing (ignored with FSR 2, which anti-aliases itself)."));
    items.push_back(choice("Anisotropic filtering", "texture.anisotropy",
        {{"1", "Off"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}},
        "Sharper textures at grazing angles (roads), on mipmapped textures."));
    if (!game) {
        items.push_back(choice("Texture filter", "texture.filter",
            {{"point", "Point"}, {"bilinear", "Bilinear"}, {"trilinear", "Trilinear"}}, "3D texture filtering."));
        items.push_back(choice("Shadows", "effects.shadows", kEffectLevels, "Shadow map resolution and filtering."));
        items.push_back(choice("Reflections", "effects.reflections", kEffectLevels, "Environment reflections."));
        items.push_back(choice("Bloom", "effects.bloom", kEffectLevels, "Bloom and glare."));
        items.push_back(choice("Depth of field", "effects.dof", kEffectLevels, "Depth of field."));
        items.push_back(choice("Motion blur", "effects.motion_blur", kEffectLevels, "Per-pixel motion blur."));
        items.push_back(choice("Tone mapping", "post.tonemapper",
            {{"original", "Original"}, {"reinhard", "Reinhard"}, {"aces", "ACES"}, {"agx", "AgX"},
             {"agx_punchy", "AgX punchy"}, {"pbr_neutral", "PBR Neutral"}}, "HDR to display."));
    }

    items.push_back(header("DISPLAY"));
    items.push_back(choice("Window", "display.mode",
        {{"windowed", "Windowed"}, {"borderless", "Borderless fullscreen"}, {"exclusive", "Exclusive fullscreen"}},
        "Also Alt+Enter or F11."));
    items.push_back(choice("V-sync", "display.vsync",
        {{"on", "On"}, {"off", "Off"}, {"adaptive", "Adaptive"}, {"mailbox", "Mailbox"}},
        "On: no tearing. Off: lowest latency, may tear. Adaptive: tears only when late. Mailbox: no tearing, "
        "newest frame."));
    items.push_back(choice("Frame rate limit", "display.fps_limit",
        {{"0", "Off"}, {"30", "30"}, {"60", "60"}, {"120", "120"}, {"144", "144"}, {"240", "240"}},
        game ? "Caps the host's presentation. The game itself always runs at 59.94 Hz."
             : "Caps the frame rate (the simulation stays at 60 Hz)."));
    items.push_back(choice("Aspect ratio", "aspect.ratio",
        {{"window", "Fit window"}, {"4:3", "4:3"}, {"16:9", "16:9"}, {"16:10", "16:10"}, {"21:9", "21:9"},
         {"32:9", "32:9"}},
        game ? "Wider than 4:3 switches on the game's own widescreen mode (Hor+: more to the sides)."
             : "Content shape; other window shapes get black bars.",
        game));
    if (game)
        items.push_back(choice("Menu layout", "aspect.hud", {{"anchored", "Centred 4:3"}, {"stretched", "Stretched (PS2)"}},
            "In widescreen: the menus in a centred 4:3 area, or stretched across the screen as on a PS2 with a "
            "widescreen TV. The race HUD always uses the full width.",
            true));
    else
        items.push_back(choice("HUD layout", "aspect.hud",
            {{"anchored", "Anchored"}, {"centered", "Centred 4:3"}, {"stretched", "Stretched"}}, "2D layout."));

    if (game) {
        items.push_back(header("GAMEPLAY"));
        items.push_back(choice("AI catch-up", "gameplay.ai_catchup",
            {{"original", "Original"}, {"reduced", "Reduced"}, {"off", "Off"}},
            "GT4 speeds up the cars behind you and slows down the cars ahead in the events that ask for it "
            "(rubber banding). Original: as on the PS2. Reduced: half as strong. Off: every car at its own pace."));
    }

    items.push_back(header("IMAGE"));
    items.push_back(step("Film grain", "post.film_grain", 0.05f, 0.0f, 1.0f, true, "Film grain strength."));
    items.push_back(step("Vignette", "post.vignette", 0.05f, 0.0f, 1.0f, true, "Darkens the corners."));
    items.push_back(step("Chromatic aberration", "post.chromatic_aberration", 0.05f, 0.0f, 1.0f, true,
        "Colour fringes towards the edges, as a lens."));
    items.push_back(choice("Dither", "post.dither", kOnOff, "Noise before quantising to the display: removes banding."));
    items.push_back(choice("Performance readout", "debug.overlay", kOnOff,
        "The renderer's settings and GPU time in a corner of the screen (also F1)."));

    items.push_back(header(""));
    items.push_back(action("Resume", 1, game ? "Back to the game." : "Close the menu."));
    items.push_back(action(game ? "Quit game" : "Quit", 2, game ? "Closes the game. Progress not saved to the "
                                                                   "memory card is lost." : "Closes the host."));
    return items;
}

std::string formatStep(const Item& item, float value) {
    char text[32];
    if (item.percent) std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    else if (item.key == "render.scale") std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(value)));
    else std::snprintf(text, sizeof text, "%g", static_cast<double>(value));
    return text;
}

}  // namespace

SettingsMenu::SettingsMenu(bool game) : items_(buildItems(game)) {
    move(1);   // the first selectable row
}

SettingsMenu::~SettingsMenu() = default;

void SettingsMenu::open() {
    open_ = true;
}

void SettingsMenu::move(int direction) {
    const size_t count = items_.size();
    size_t index = selected_;
    for (size_t tries = 0; tries < count; ++tries) {
        index = (index + count + static_cast<size_t>(direction)) % count;
        if (items_[index].kind != Item::Header) { selected_ = index; return; }
    }
}

void SettingsMenu::setStartValues(const Settings& settings) {
    startValues_.clear();
    for (const auto& item : items_)
        startValues_.push_back(item.restart ? getOption(settings, item.key) : std::string());
}

bool SettingsMenu::restartPending(const Settings& settings) const {
    for (size_t i = 0; i < items_.size() && i < startValues_.size(); ++i)
        if (items_[i].restart && getOption(settings, items_[i].key) != startValues_[i]) return true;
    return false;
}

SettingsMenu::Result SettingsMenu::input(Input input, Settings& settings) {
    Result result;
    if (!open_) return result;
    const Item& item = items_[selected_];
    int direction = 0;
    switch (input) {
    case Input::Up: move(-1); return result;
    case Input::Down: move(1); return result;
    case Input::PageUp:
    case Input::PageDown: {
        // To the first selectable row of the previous / next section.
        const int dir = input == Input::PageUp ? -1 : 1;
        size_t index = selected_;
        bool crossed = false;
        while (true) {
            if ((dir < 0 && index == 0) || (dir > 0 && index + 1 >= items_.size())) break;
            index = static_cast<size_t>(static_cast<int>(index) + dir);
            if (items_[index].kind == Item::Header) { crossed = true; continue; }
            if (crossed) {
                if (dir < 0)   // the top of that section
                    while (index > 0 && items_[index - 1].kind != Item::Header) --index;
                selected_ = index;
                break;
            }
        }
        return result;
    }
    case Input::Back: open_ = false; result.closed = true; return result;
    case Input::Left: direction = -1; break;
    case Input::Right: direction = 1; break;
    case Input::Accept:
        if (item.kind == Item::Action) {
            open_ = false;
            result.closed = true;
            result.quit = item.action == 2;
            return result;
        }
        direction = 1;
        break;
    }
    if (item.kind == Item::Choice) {
        const std::string current = getOption(settings, item.key);
        // "custom" is shown when the values make it so, never chosen.
        std::vector<std::string> values;
        for (const auto& [value, label] : item.choices)
            if (value != "custom") values.push_back(value);
        auto found = std::find(values.begin(), values.end(), current);
        const int count = static_cast<int>(values.size());
        int index = found == values.end() ? (direction > 0 ? -1 : count) : static_cast<int>(found - values.begin());
        index = std::clamp(index + direction, 0, count - 1);
        if (values[static_cast<size_t>(index)] == current) return result;
        result.changed = setOption(settings, item.key, values[static_cast<size_t>(index)]).empty();
    } else if (item.kind == Item::Step) {
        float value = std::strtof(getOption(settings, item.key).c_str(), nullptr);
        if (item.key == "render.scale" && getOption(settings, "render.quality") != "custom")
            value = std::round(100.0f / upscaleRatio(settings));
        const float next = std::clamp(std::round((value + item.step * static_cast<float>(direction)) / item.step) * item.step,
                                      item.minimum, item.maximum);
        char text[32];
        std::snprintf(text, sizeof text, "%g", static_cast<double>(next));
        if (item.key == "render.scale") result.changed |= setOption(settings, "render.quality", "custom").empty();
        result.changed |= setOption(settings, item.key, text).empty();
    }
    return result;
}

MenuView SettingsMenu::view(const Settings& settings) const {
    MenuView view;
    view.title = "SETTINGS";
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& item = items_[i];
        MenuRow row;
        row.label = item.label;
        row.header = item.kind == Item::Header;
        row.selected = i == selected_;
        row.adjustable = item.kind == Item::Choice || item.kind == Item::Step;
        row.restart = item.restart;
        if (item.kind == Item::Choice) {
            const std::string current = getOption(settings, item.key);
            row.value = current;
            for (const auto& [value, label] : item.choices)
                if (value == current) row.value = label;
        } else if (item.kind == Item::Step) {
            float value = std::strtof(getOption(settings, item.key).c_str(), nullptr);
            if (item.key == "render.scale") value = std::round(100.0f / upscaleRatio(settings));
            row.value = formatStep(item, value);
        }
        if (row.selected) view.help = item.help;
        view.rows.push_back(std::move(row));
    }
    view.footer = "Up/Down: choose   Left/Right: change   L1/R1, PgUp/PgDn: section   Esc: close";
    if (restartPending(settings)) view.footer = "* applies when the game starts again.   " + view.footer;
    return view;
}

}  // namespace gt4
