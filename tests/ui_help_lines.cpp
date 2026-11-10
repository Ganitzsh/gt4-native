// MENU_DESIGN M13: the selected row's help in its band above the prompts, wrapped by the host over three lines at
// most. With the real fonts (the host's SDF faces) and the host's own schema (each row's help: the option's own, or
// its shorter menu text, settings_schema.cpp's menuHelp), for every row
// of every tab at the 4:3, 16:10 and 16:9 canvases (1440, 1728 and 1920 units wide; the Odin's 1280 x 800 is 16:10):
// the band's box holds exactly three of the body face's lines, and a help text that needs more than three is listed
// against tests/ui/help_over_three_lines.txt. A new one then shows as a difference, to be shortened at its source
// (or the list rewritten with --update, after a look). Sub-pages' rows are not walked (their help is written with
// their page). No GPU, no window.
//   gt4_ui_help_lines_test GOLDEN_DIR [--update]
#include "menu_layout.h"
#include "menu_model.h"
#include "sdf_font.h"
#include "settings.h"
#include "settings_schema.h"
#include "ui_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string &what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// Measures with the real faces; draws nothing.
struct FontSink : gt4::ui::PageSink {
    gt4::ui::SdfFont font;
    void rect(const gt4::ui::PageRect &, gt4::ui::PageColor, gt4::ui::PageColor) override {}
    float textWidth(gt4::ui::FontFace face, float em, std::string_view text) override {
        return font.measure(face, em, text);
    }
    float lineHeight(gt4::ui::FontFace face, float em) override { return font.lineHeight(face, em); }
    void text(float, float, float, gt4::ui::FontFace, std::string_view, gt4::ui::PageColor) override {}
    void clip(const gt4::ui::PageRect *) override {}
};
}  // namespace

int main(int argc, char **argv) {
    using namespace gt4::ui;
    const std::string goldens = argc > 1 ? argv[1] : "tests/ui";
    const bool update = argc > 2 && std::string(argv[2]) == "--update";

    const gt4::Settings settings;
    const Schema schema = buildSchema(settings);
    FontSink sink;
    const FontFace body = pageFace(Font::Body, false);
    std::ostringstream listed;
    int bands = 0, threeLines = 0, rows = 0;
    for (const float width : {1440.0f, 1728.0f, 1920.0f}) {
        for (size_t tab = 0; tab < schema.tabs.size(); ++tab) {
            Menu menu;
            menu.setSchema(schema);
            menu.open();
            for (size_t i = 0; i < tab; ++i) menu.press(Input::TabRight);
            menu.tick(5.0f);
            std::set<int> seen;
            for (int step = 0; step < 200 && menu.page() && menu.row() >= 0 && seen.insert(menu.row()).second;
                 ++step) {
                const Row &row = menu.page()->rows[static_cast<size_t>(menu.row())];
                LayoutOptions options;
                options.canvasWidth = width;
                menu.tick(1.0f);   // past the page's slide: the band is laid out where it rests
                const DrawList list = layout(menu, options);
                for (const Item &item : list.items) {
                    if (item.kind != ItemKind::Text || item.font != Font::Body || item.y != 850.0f) continue;
                    ++bands;
                    const float line = sink.lineHeight(body, item.size);
                    // (as ui_renderer counts them)
                    const int holds = std::max(1, static_cast<int>(std::floor(item.h / line + 0.01f)));
                    threeLines += holds == 3 ? 1 : 0;
                    // (the row's help itself, in the band's box: on a canvas without the panel a row not used
                    // with these settings shows why in the band, and its help must fit all the same once it is used)
                    const size_t needs = std::max(wrapText(sink, body, item.size, item.text, item.w, 1000).size(),
                                                  wrapText(sink, body, item.size, row.help, item.w, 1000).size());
                    if (needs > 3)
                        listed << static_cast<int>(width) << ' ' << schema.tabs[tab] << ' ' << row.key << ": "
                               << needs << " lines\n";
                }
                ++rows;
                menu.press(Input::Down);
                menu.tick(1.0f);
            }
        }
    }
    check(rows > 0 && bands == rows, "every selected row's help in the band (" + std::to_string(bands) + " of " +
                                         std::to_string(rows) + ")");
    check(threeLines == bands, "the band's box holds three body lines, not two or four (" +
                                   std::to_string(threeLines) + " of " + std::to_string(bands) + ")");

    const std::string path = goldens + "/help_over_three_lines.txt";
    if (update) {
        std::ofstream(path, std::ios::binary) << listed.str();
    } else {
        std::ifstream in(path, std::ios::binary);
        const std::string golden((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (golden != listed.str()) {
            std::fprintf(stderr,
                         "help texts over three lines differ from %s: shorten the new ones at their source (or "
                         "--update after a look):\n%s\n",
                         path.c_str(), listed.str().c_str());
            ++failures;
        }
    }
    std::printf("%d rows' help over %d tabs at three canvases; over three lines:\n%s", rows,
                static_cast<int>(schema.tabs.size()), listed.str().c_str());
    return failures == 0 ? 0 : 1;
}
