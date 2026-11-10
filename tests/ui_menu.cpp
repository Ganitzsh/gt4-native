// The settings menu's page engine and layout (platform/ui, docs/modern/MENU_DESIGN.md M2): navigation, held-direction
// repeat, tab memory, apply classes, the keep-or-revert dialog, previews, resets, and the layout at 16:9, 21:9 and
// 4:3 against golden draw lists (text). No GPU, no window, no game data.
//   gt4_ui_menu_test GOLDEN_DIR [--update]   (--update rewrites the goldens; check the diff before committing)
#include "draw_list.h"
#include "menu_layout.h"
#include "menu_model.h"
#include "pause_page.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace gt4::ui;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

Row header(const char* label) {
    Row row;
    row.kind = RowKind::Header;
    row.label = label;
    return row;
}
Row choice(const char* key, const char* label, std::vector<ChoiceValue> choices, const char* value,
           ApplyClass apply = ApplyClass::Live) {
    Row row;
    row.kind = RowKind::Choice;
    row.key = key;
    row.label = label;
    row.help = std::string("Help for ") + label + ".";
    row.choices = std::move(choices);
    row.value = value;
    row.apply = apply;
    return row;
}
Row step(const char* key, const char* label, float minimum, float maximum, float size, const char* value,
         const char* unit = "") {
    Row row;
    row.kind = RowKind::Step;
    row.key = key;
    row.label = label;
    row.help = std::string("Help for ") + label + ".";
    row.minimum = minimum;
    row.maximum = maximum;
    row.step = size;
    row.value = value;
    row.unit = unit;
    return row;
}

Schema schema() {
    Schema s;
    Page graphics;
    graphics.id = "graphics";
    graphics.title = "Graphics";
    graphics.icon = "tab.graphics";
    graphics.livePreview = true;
    graphics.rows.push_back(header("Quality"));
    Row preset = choice("graphics.preset", "Preset",
                        {{"original", "Original"}, {"low", "Low"}, {"high", "High"}, {"ultra", "Ultra"}}, "high");
    preset.previewFirst = true;
    graphics.rows.push_back(preset);
    Row scale = step("render.scale", "Resolution scale", 50, 200, 10, "100", "%");
    scale.presetValue = "100";
    graphics.rows.push_back(scale);
    graphics.rows.push_back(
        choice("render.aa", "Anti-aliasing", {{"off", "Off"}, {"fxaa", "FXAA"}, {"taa", "TAA"}}, "taa"));
    graphics.rows.push_back(header("Detail"));
    Row frames = choice("display.race_frames", "Race frames", {{"fields", "Fields"}, {"progressive", "Whole frames"}},
                        "progressive", ApplyClass::NextRace);
    frames.pending = true;
    graphics.rows.push_back(frames);
    Row ao = choice("effects.ambient_occlusion", "Ambient occlusion", {{"off", "Off"}, {"high", "High"}}, "high");
    ao.unused = "Not used by the PS2-resolution renderer.";
    graphics.rows.push_back(ao);
    Row pack = choice("texture.hd_pack", "HD textures", {{"on", "On"}, {"off", "Off"}}, "off");
    pack.enabled = false;
    graphics.rows.push_back(pack);

    Page display;
    display.id = "display";
    display.title = "Display";
    display.icon = "tab.display";
    display.rows.push_back(header("Window"));
    display.rows.push_back(choice("display.mode", "Window mode",
                                  {{"windowed", "Windowed"}, {"borderless", "Borderless"}, {"exclusive", "Exclusive"}},
                                  "windowed", ApplyClass::Confirm));
    Row link;
    link.kind = RowKind::Link;
    link.key = "display.hdr";
    link.label = "HDR";
    link.help = "HDR output and its calibration.";
    display.rows.push_back(link);
    Row quit;
    quit.kind = RowKind::Action;
    quit.key = "quit";
    quit.label = "Quit game";
    display.rows.push_back(quit);

    Page hdr;
    hdr.id = "display.hdr";
    hdr.title = "HDR";
    hdr.rows.push_back(choice("display.hdr", "HDR", {{"off", "Off"}, {"on", "On"}}, "off", ApplyClass::Confirm));
    hdr.rows.push_back(step("display.hdr_paper_white", "Paper white", 80, 400, 10, "200", "nits"));

    s.pages = {graphics, display, hdr};
    s.tabs = {"graphics", "display"};
    return s;
}

bool hasRequest(const Output& out, Request::Kind kind, const char* key, const char* value) {
    for (const auto& r : out.requests)
        if (r.kind == kind && r.key == key && r.value == value) return true;
    return false;
}

std::string read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}
}  // namespace

int main(int argc, char** argv) {
    const std::string goldens = argc > 1 ? argv[1] : "tests/ui";
    const bool update = argc > 2 && std::string(argv[2]) == "--update";

    // Navigation: the first selectable row after the header; Up from the first wraps; a held repeat stops at the end.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        check(menu.row() == 1, "the first selectable row is selected on open");
        Output out = menu.press(Input::Down);
        check(menu.row() == 2 && out.sound == Sound::Cursor, "Down moves one row with the cursor sound");
        menu.press(Input::Down);
        menu.press(Input::Down);
        check(menu.row() == 5, "Down skips a header");
        menu.press(Input::Up);
        menu.press(Input::Up);
        menu.press(Input::Up);
        check(menu.row() == 1, "Up back to the first row");
        menu.press(Input::Up);
        check(menu.row() == 7, "Up from the first row wraps to the last");
        menu.hold(Input::Down, true);
        menu.tick(5.0f);
        check(menu.row() == 7, "a held repeat does not wrap");
        menu.hold(Input::Down, false);
    }

    // Repeat: first after 20 VBlanks (0.333 s), then every 6 (0.1 s), as GT4's menus.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.hold(Input::Down, true);
        menu.tick(0.30f);
        check(menu.row() == 1, "no repeat before the delay");
        menu.tick(0.06f);
        check(menu.row() == 2, "the first repeat after the delay");
        menu.tick(0.11f);
        check(menu.row() == 3, "the next repeat one interval later");
        menu.hold(Input::Down, false);
        menu.tick(1.0f);
        check(menu.row() == 3, "nothing after the release");
    }

    // A held Left / Right on a Step row speeds up after a second.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::Down);   // Resolution scale, 100
        menu.hold(Input::Right, true);
        int sets = 0;
        for (int i = 0; i < 100; ++i) sets += static_cast<int>(menu.tick(0.01f).requests.size());   // 1.0 s
        const int slow = sets;
        sets = 0;
        for (int i = 0; i < 13; ++i) sets += static_cast<int>(menu.tick(0.01f).requests.size());    // 0.13 s more
        // Slow repeats at 0.33, 0.43 ... 0.93 s (7, so 170); at 0.04 s the last three steps to 200 come at 1.04,
        // 1.08 and 1.12 s, where 0.1 s would give one (1.03 s).
        check(slow == 7 && sets == 3, "Step repeats: 7 in the first second, then one every 0.04 s up to the maximum");
        menu.hold(Input::Right, false);
        check(menu.page()->rows[2].value == "200", "the held Right stops at the maximum");
    }

    // Tabs keep their row; a sub-page closes on reopen; tabs wrap.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::Down);
        menu.press(Input::Down);
        menu.press(Input::TabRight);
        check(menu.tab() == 1 && menu.row() == 1, "the second tab opens on its first row");
        menu.press(Input::Down);   // HDR link
        Output out = menu.press(Input::Confirm);
        check(menu.inSubPage() && menu.page()->id == "display.hdr" && out.sound == Sound::Ok, "a Link opens its page");
        menu.press(Input::Back);
        check(!menu.inSubPage() && menu.row() == 2, "Back returns to the row that opened it");
        menu.press(Input::Confirm);
        menu.press(Input::Close);
        check(!menu.isOpen(), "Close closes from a sub-page");
        menu.open();
        check(menu.tab() == 1 && !menu.inSubPage() && menu.row() == 2, "reopening returns to the last tab and row");
        menu.press(Input::TabRight);
        check(menu.tab() == 0 && menu.row() == 3, "tabs wrap, and the first tab kept its row");
        Output closed = menu.press(Input::Back);
        check(closed.closed && !menu.isOpen() && closed.sound == Sound::Cancel, "Back on a tab closes the menu");
    }

    // The race pause's time limit (pause_page.h; M7, M4): it counts on the pause's own pages without input, never on
    // a Settings tab, and again once back on the pause tab. (GT4's PauseBase::setTimeLimit has one caller,
    // RaceBasic::pauseStart, and RaceInformation's pause limit is 0 but in race information read from a stream: no
    // mode measured has one, so this rule is checked here.)
    {
        Schema s;
        Page pause;
        pause.id = "pause";
        Row cont;
        cont.kind = RowKind::Action;
        cont.key = "pause.continue";
        pause.rows.push_back(cont);
        Page settings;
        settings.id = "graphics";
        Row aa;
        aa.kind = RowKind::Choice;
        aa.key = "render.aa";
        aa.choices = {{"off", "Off"}, {"taa", "TAA"}};
        aa.value = "off";
        settings.rows.push_back(aa);
        Page screen;
        screen.id = "pause.screen";
        s.pages = {pause, settings, screen};
        s.tabs = {"pause", "graphics"};
        Menu menu;
        menu.setSchema(s);
        menu.open();
        check(isPausePage(menu.page()) && pauseTimeLimitCounts(menu, false), "the limit counts on the pause tab");
        check(!pauseTimeLimitCounts(menu, true), "input clears it there, as GT4's strobe does");
        menu.selectTab("graphics");
        check(!isPausePage(menu.page()) && !pauseTimeLimitCounts(menu, false),
              "never while a Settings tab is shown, input or not");
        menu.selectTab("pause");
        check(pauseTimeLimitCounts(menu, false), "and it counts again back on the pause tab");
    }

    // selectTab: a tab by id, from its root, its row kept (the pause page's Settings and Back, M7).
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::TabRight);
        menu.press(Input::Down);
        menu.press(Input::Confirm);   // into the HDR sub-page
        const std::string first = schema().tabs.front(), second = schema().tabs[1];
        check(menu.selectTab(first) && menu.tab() == 0 && !menu.inSubPage(), "selectTab leaves the sub-page");
        check(menu.selectTab(second) && menu.tab() == 1 && menu.row() == 2, "selectTab keeps the tab's row");
        check(!menu.selectTab("nowhere") && menu.tab() == 1, "an unknown tab is refused");
    }

    // Live: a change is a Set request at once.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::Down);
        menu.press(Input::Down);   // Anti-aliasing, taa
        Output out = menu.press(Input::Left);
        check(hasRequest(out, Request::Kind::Set, "render.aa", "fxaa") && out.sound == Sound::Cursor,
              "Live: Set at once");
        out = menu.press(Input::Right);
        out = menu.press(Input::Right);
        check(out.requests.empty() && out.sound == Sound::Disable, "the end of a choice list: the refusal sound");
    }

    // Confirm: the dialog takes the inputs, reverts after 15 s, or keeps on Confirm, or reverts on Back.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::TabRight);
        Output out = menu.press(Input::Right);
        check(hasRequest(out, Request::Kind::Set, "display.mode", "borderless") && menu.confirming(),
              "Confirm class: Set, then the dialog");
        out = menu.press(Input::Down);
        check(out.requests.empty() && menu.row() == 1, "the dialog takes the other inputs");
        out = menu.tick(14.9f);
        check(menu.confirming() && out.requests.empty(), "the dialog waits 15 s");
        out = menu.tick(0.2f);
        check(!menu.confirming() && hasRequest(out, Request::Kind::Revert, "display.mode", "windowed") &&
              menu.page()->rows[1].value == "windowed", "it reverts when the time runs out");
        menu.press(Input::Right);
        out = menu.press(Input::Confirm);
        check(!menu.confirming() && out.requests.size() == 1 && out.sound == Sound::Ok &&
                  hasRequest(out, Request::Kind::Action, gt4::ui::kConfirmKeep, "display.mode"),
              "Confirm keeps the new value, and tells the host which key it kept (M4: saved then)");
        menu.press(Input::Right);
        out = menu.press(Input::Back);
        check(!menu.confirming() && hasRequest(out, Request::Kind::Revert, "display.mode", "borderless"),
              "Back in the dialog reverts to the value before");
    }

    // Preview first (the preset): Left / Right show a pending value; Confirm applies it, Back drops it.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        Output out = menu.press(Input::Right);
        check(out.requests.empty() && menu.previewing() && menu.shownValue(menu.page()->rows[1]) == "ultra",
              "a preview sends nothing");
        out = menu.press(Input::Confirm);
        check(hasRequest(out, Request::Kind::Set, "graphics.preset", "ultra") && !menu.previewing(),
              "Confirm applies it");
        menu.press(Input::Left);
        out = menu.press(Input::Back);
        check(out.requests.empty() && !menu.previewing() && menu.isOpen() &&
              menu.shownValue(menu.page()->rows[1]) == "ultra", "Back drops the preview and keeps the menu open");
    }

    // Reset to the preset's value; disabled rows refuse; the host's values replace the shown ones.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::Down);
        menu.press(Input::Right);
        Output out = menu.press(Input::Reset);
        check(hasRequest(out, Request::Kind::Set, "render.scale", "100"), "Reset sends the preset's value");
        out = menu.press(Input::Reset);
        check(out.requests.empty() && out.sound == Sound::Disable, "Reset at the preset's value is refused");
        menu.press(Input::Up);
        menu.press(Input::Up);   // wraps to HD textures (disabled)
        out = menu.press(Input::Right);
        check(out.requests.empty() && out.sound == Sound::Disable, "a disabled row refuses changes");
        menu.setValue("render.aa", "off");
        check(menu.page()->rows[3].value == "off", "setValue updates the row");
    }

    // Formatting.
    {
        Row r = step("a", "A", 0, 1, 0.05f, "0.2");
        r.percent = true;
        check(formatStep(r, "0.2") == "20 %", "a percent step");
        check(formatStep(step("b", "B", 0, 500, 10, "150", "ms"), "150") == "150 ms", "a whole step with a unit");
        check(formatStep(step("c", "C", 1, 3, 0.1f, "1.4"), "1.4") == "1.4", "a fractional step");
    }

    // Layout against the goldens: 16:9, 21:9, 4:3; the second row selected, after the fade.
    for (float width : {1920.0f, 2520.0f, 1440.0f}) {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::Down);
        menu.tick(1.0f);
        LayoutOptions options;
        options.canvasWidth = width;
        const DrawList list = layout(menu, options);
        bool inside = true;
        for (const auto& item : list.items)
            inside = inside && item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= width + 0.01f &&
                     item.y + item.h <= kCanvasHeight + 0.01f;
        check(inside, "every item inside the canvas");
        bool panel = false;
        // (by its frame: a nine-slice 620 wide; until the art is packed no item names an asset, kUiAssetsPacked)
        for (const auto& item : list.items) panel = panel || (item.kind == ItemKind::NineSlice && item.w == 620.0f);
        check(panel == (width >= 1900.0f), "the detail panel only where the canvas is wide enough");
        const std::string text = describe(list);
        char name[64];
        std::snprintf(name, sizeof name, "/menu_%d.txt", static_cast<int>(width));
        const std::string path = goldens + name;
        if (update) {
            std::ofstream(path, std::ios::binary) << text;
        } else {
            const std::string golden = read(path);
            if (golden != text) {
                std::fprintf(stderr, "layout differs from %s (--update rewrites it):\n%s\n", path.c_str(),
                             text.c_str());
                ++failures;
            }
        }
    }

    // The confirm dialog is laid out over everything.
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.press(Input::TabRight);
        menu.press(Input::Right);
        menu.tick(5.2f);
        const DrawList list = layout(menu, LayoutOptions());
        bool dialog = false, countdown = false;
        for (const auto& item : list.items) {
            dialog = dialog || (item.kind == ItemKind::NineSlice && item.w == 760.0f);   // its frame
            countdown = countdown || item.text == "Going back to the previous ones in 10 s";
        }
        check(dialog && countdown, "the dialog with its countdown");
    }

    // The hub's eight tabs (M4's titles) at 16:9, 21:9 and 4:3: every item inside the canvas (the chips shrink to
    // fit); the help in its band above the prompts, the page's width and three lines high (M13), the prompts where
    // they were (their row at 1008), in every prompt style; a golden draw list per tab at 4:3 and 16:9.
    {
        Schema s;
        for (const char* title : {"Game", "Graphics", "Display", "Image", "Controls", "Audio", "Accessibility",
                                  "System"}) {
            Page p;
            p.id = title;
            p.title = title;
            Row r;
            r.kind = RowKind::Choice;
            r.key = std::string(title) + ".row";
            r.label = "A row";
            r.choices = {{"a", "A"}, {"b", "B"}};
            r.value = "a";
            r.presetValue = "a";
            r.help = "A help line long enough to run into the prompts if nothing stopped it at their left edge, "
                     "and then some more words.";
            p.rows.push_back(r);
            s.pages.push_back(p);
            s.tabs.push_back(title);
        }
        Menu menu;
        menu.setSchema(s);
        menu.open();
        menu.tick(5.0f);
        for (const float width : {1440.0f, 1920.0f, 2520.0f})
            for (const PromptStyle style : {PromptStyle::Xbox, PromptStyle::PlayStation, PromptStyle::Keyboard}) {
                LayoutOptions options;
                options.canvasWidth = width;
                options.prompts = style;
                const DrawList list = layout(menu, options);
                bool inside = true, help = false, promptsRow = false;
                for (const auto& item : list.items) {
                    if (item.kind == ItemKind::ClipPush || item.kind == ItemKind::ClipPop) continue;
                    inside = inside && item.x >= 0.0f && item.x + item.w <= width + 0.01f;
                    if (item.kind == ItemKind::Text && item.text == menu.page()->rows[0].help)
                        help = item.y == 850.0f && item.x == 80.0f && item.w == width - 160.0f &&
                               item.h == 125.0f && item.size == 26.0f && item.y + item.h <= 1008.0f;
                    promptsRow = promptsRow || (item.kind == ItemKind::Text && item.y == 1014.0f);
                }
                check(inside, "the eight tabs and the caps inside the canvas");
                check(help, "the help in its band: the page's width, a three-line box, above the prompts");
                check(promptsRow, "the prompts' labels where they were (1014)");
            }
        for (const float width : {1440.0f, 1920.0f})
            for (int tab = 0; tab < static_cast<int>(s.tabs.size()); ++tab) {
                Menu m;
                m.setSchema(s);
                m.open();
                for (int i = 0; i < tab; ++i) m.press(Input::TabRight);
                m.tick(5.0f);
                LayoutOptions options;
                options.canvasWidth = width;
                const std::string text = describe(layout(m, options));
                char name[96];
                std::snprintf(name, sizeof name, "/tab_%s_%d.txt", s.tabs[static_cast<size_t>(tab)].c_str(),
                              static_cast<int>(width));
                const std::string path = goldens + name;
                if (update) {
                    std::ofstream(path, std::ios::binary) << text;
                } else if (read(path) != text) {
                    std::fprintf(stderr, "layout differs from %s (--update rewrites it):\n%s\n", path.c_str(),
                                 text.c_str());
                    ++failures;
                }
            }
    }

    // Until the art is packed (kUiAssetsPacked, M10): no item names an asset; the prompts are key caps with the
    // button's name as text, per controller (the architect: words, no console symbols).
    {
        Menu menu;
        menu.setSchema(schema());
        menu.open();
        menu.tick(5.0f);
        for (const PromptStyle style : {PromptStyle::Xbox, PromptStyle::PlayStation, PromptStyle::Keyboard}) {
            LayoutOptions options;
            options.prompts = style;
            const DrawList list = layout(menu, options);
            bool named = false, caps = false;
            for (const auto& item : list.items) {
                named = named || item.asset != 0;
                caps = caps || (item.kind == ItemKind::Text && item.text == buttonName(style, "confirm"));
            }
            check(kUiAssetsPacked || !named, "no asset named before the art is packed");
            check(kUiAssetsPacked || caps,
                  (std::string("the confirm key cap reads ") + buttonName(style, "confirm")).c_str());
        }
        check(std::string(buttonName(PromptStyle::Xbox, "back")) == "B" &&
                  std::string(buttonName(PromptStyle::PlayStation, "back")) == "Circle" &&
                  std::string(buttonName(PromptStyle::Keyboard, "back")) == "C",
              "back: B, Circle, the keyboard's C");
    }

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("ui_menu: all checks passed\n");
    return failures ? 1 : 0;
}
