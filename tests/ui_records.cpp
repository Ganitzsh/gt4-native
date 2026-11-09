// The settings pages' link records (include/gt4/ui.h, platform/ui/ui_records.h; MENU_DESIGN.md M1): the wire
// layouts, a round trip of each record, the 64 KiB cap on a draw list, and malformed records refused.
#include "draw_list.h"
#include "menu_model.h"
#include "ui_records.h"

#include <gt4/link/protocol.h>
#include <gt4/ui.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt4::ui;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
}  // namespace

int main() {
    // Layouts: the same in the 32-bit game and the 64-bit host (no 8-byte members, fixed offsets).
    check(sizeof(wire::UiHeader) == 28 && offsetof(wire::UiHeader, canvasWidth) == 12 &&
              offsetof(wire::UiHeader, dropped) == 24, "UiHeader layout");
    check(sizeof(wire::UiItem) == 52 && offsetof(wire::UiItem, weight) == 4 && offsetof(wire::UiItem, color2) == 32 &&
              offsetof(wire::UiItem, textLength) == 48, "UiItem layout");
    check(sizeof(wire::SetHeader) == 16 && offsetof(wire::SetHeader, valueBytes) == 12, "SetHeader layout");
    check(sizeof(wire::SettingsHeader) == 16 && offsetof(wire::SettingsHeader, bytes) == 12, "SettingsHeader layout");
    check(GT4_REC_UI == 16 && GT4_REC_SETTINGS_SET == 17 && GT4_REC_SETTINGS == 68 && GT4_LINK_VERSION == 6u &&
              GT4_REC_WHEEL_FX == 18 && GT4_REC_WHEEL == 69,
          "record numbers and the link version");

    // A draw list round trip, text included.
    {
        DrawList list;
        list.flags = kDimGame;
        list.canvasWidth = 2520.0f;
        Item quad;
        quad.x = 10.5f;
        quad.w = 100.0f;
        quad.asset = assetId("row.band");
        quad.opacity = 0.5f;
        list.items.push_back(quad);
        Item text;
        text.kind = ItemKind::Text;
        text.font = Font::Title;
        text.italic = true;
        text.weight = 800;
        text.align = Align::Right;
        text.text = "Fen\xC3\xAAtre \t \\ \xE2\x80\xBA";
        list.items.push_back(text);
        Item clip;
        clip.kind = ItemKind::ClipPop;
        list.items.push_back(clip);
        std::vector<uint8_t> bytes;
        check(encodeDrawList(list, 1234, 7, 2, bytes), "a small list encodes");
        DrawList back;
        wire::UiHeader header{};
        check(decodeDrawList(bytes.data(), bytes.size(), back, &header), "and decodes");
        check(header.vblank == 1234 && header.list == 7 && header.dropped == 2 && back.canvasWidth == 2520.0f &&
                  back.flags == kDimGame, "the header's fields");
        check(describe(back) == describe(list), "every item's fields and text");
        // Truncated anywhere: refused, never read past the end.
        bool refused = true;
        for (size_t n = 0; n < bytes.size(); ++n) refused = refused && !decodeDrawList(bytes.data(), n, back);
        check(refused, "a truncated list is refused");
        std::vector<uint8_t> bad = bytes;
        bad[sizeof(wire::UiHeader)] = 99;   // the first item's kind
        check(!decodeDrawList(bad.data(), bad.size(), back), "an unknown item kind is refused");
    }

    // The cap: a list over 64 KiB is not encoded.
    {
        DrawList list;
        Item text;
        text.kind = ItemKind::Text;
        text.text = std::string(1000, 'x');
        for (int i = 0; i < 70; ++i) list.items.push_back(text);
        std::vector<uint8_t> bytes{1, 2, 3};
        check(!encodeDrawList(list, 0, 0, 0, bytes) && bytes.empty(), "a list over the cap is refused, nothing kept");
        list.items.resize(60);
        check(encodeDrawList(list, 0, 0, 0, bytes) && bytes.size() <= wire::kUiMaxBytes, "60 KB of text fits");
    }

    // The schema round trip: pages, rows of each kind, choices, escapes, flags.
    {
        Schema schema;
        schema.tabs = {"graphics", "display"};
        Page page;
        page.id = "graphics";
        page.title = "Graphics";
        page.icon = "tab.graphics";
        page.livePreview = true;
        Row header;
        header.kind = RowKind::Header;
        header.label = "Quality";
        page.rows.push_back(header);
        Row row;
        row.kind = RowKind::Step;
        row.key = "render.sharpness";
        row.label = "Sharpening";
        row.help = "Line one.\nLine two, with a tab\there and a backslash \\.";
        row.minimum = 0.0f;
        row.maximum = 1.0f;
        row.step = 0.05f;
        row.percent = true;
        row.unit = "";
        row.apply = ApplyClass::Live;
        row.value = "0.2";
        row.presetValue = "0.2";
        row.unused = "Not used now.";
        page.rows.push_back(row);
        Row choice;
        choice.kind = RowKind::Choice;
        choice.key = "display.mode";
        choice.label = "Window mode";
        choice.apply = ApplyClass::Confirm;
        choice.previewFirst = false;
        choice.enabled = false;
        choice.pending = true;
        choice.value = "windowed";
        choice.choices = {{"windowed", "Windowed"}, {"borderless", "Borderless fullscreen"}};
        page.rows.push_back(choice);
        schema.pages.push_back(page);
        Page display;
        display.id = "display";
        display.title = "Display";
        schema.pages.push_back(display);

        const std::string text = encodeSchema(schema);
        Schema back;
        check(decodeSchema(text, back), "the schema decodes");
        check(encodeSchema(back) == text, "and encodes to the same text");
        check(back.tabs == schema.tabs && back.pages.size() == 2 && back.pages[0].rows.size() == 3 &&
                  back.pages[0].livePreview, "pages, rows, tabs");
        const Row& r = back.pages[0].rows[1];
        check(r.help == row.help && r.step == 0.05f && r.percent && r.unused == row.unused, "a row's text and numbers");
        const Row& c = back.pages[0].rows[2];
        check(c.apply == ApplyClass::Confirm && !c.enabled && c.pending && c.choices.size() == 2 &&
                  c.choices[1].label == "Borderless fullscreen", "a choice row's flags and choices");
        check(decodeSchema(text + "future\tline\n", back) && back.pages.size() == 2, "unknown line types are skipped");
        check(!decodeSchema("tabs\nrow\t1\tonly\n", back), "a row before any page, or short, is refused");
    }

    // Values, settings and set records.
    {
        const std::vector<ValueUpdate> values = {{"render.aa", "fxaa", "", false},
                                                 {"display.race_frames", "fields", "", true},
                                                 {"effects.ambient_occlusion", "high", "Not used\tnow.", false}};
        std::vector<ValueUpdate> back;
        check(decodeValues(encodeValues(values), back) && back.size() == 3 && back[1].pending &&
                  back[2].unused == "Not used\tnow.", "values round trip");

        const auto settings = encodeSettings(wire::kResult, 41, false, "No such option: x.y");
        wire::SettingsHeader header{};
        std::string body;
        check(decodeSettings(settings.data(), settings.size(), header, body) && header.kind == wire::kResult &&
                  header.request == 41 && header.ok == 0 && body == "No such option: x.y", "a result round trip");
        check(!decodeSettings(settings.data(), settings.size() - 1, header, body), "a short settings record is refused");

        const auto set = encodeSet(42, wire::kSet, "render.scale", "77");
        wire::SetHeader setHeader{};
        std::string key, value;
        check(decodeSet(set.data(), set.size(), setHeader, key, value) && setHeader.request == 42 &&
                  setHeader.kind == wire::kSet && key == "render.scale" && value == "77", "a set round trip");
        check(!decodeSet(set.data(), set.size() - 1, setHeader, key, value), "a short set record is refused");
        auto badSet = set;
        badSet[4] = 9;   // kind
        check(!decodeSet(badSet.data(), badSet.size(), setHeader, key, value), "an unknown set kind is refused");
    }

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("ui_records: all checks passed\n");
    return failures ? 1 : 0;
}
