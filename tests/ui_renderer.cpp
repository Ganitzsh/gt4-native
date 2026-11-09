// The settings pages' renderer (platform/ui/host/ui_renderer, MENU_DESIGN.md M3), on a recording sink: the canvas
// mapped into the content rectangle at 16:9, 21:9 and 4:3 (centred: a narrower canvas lives inside the pillarbox),
// colour times opacity, gradients, the text's alignment in its box and the ellipsis cut at a code point, clip nesting
// (intersected, popped, an unbalanced pop ignored and noted once), unknown assets drawn flat and noted once; and the
// hub M2 lays out drawn whole, every text inside its box and every clip popped.
#include "menu_layout.h"
#include "ui_renderer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const std::string &what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

using namespace gt4::ui;

// Text is half an em per code point, so widths are exact.
struct Recorder : PageSink {
    struct Call {
        char kind = 0;   // 'r' rect, 't' text, 'c' clip, 'n' no clip
        PageRect rect;
        PageColor top, bottom;
        float x = 0, top_ = 0, em = 0;
        FontFace face = FontFace::Row;
        std::string text;
    };
    std::vector<Call> calls;
    static float widthOf(float em, std::string_view text) {
        int points = 0;
        for (unsigned char c : text) points += (c & 0xC0) != 0x80;
        return points * em * 0.5f;
    }
    void rect(const PageRect &rect, PageColor top, PageColor bottom) override {
        Call call;
        call.kind = 'r';
        call.rect = rect;
        call.top = top;
        call.bottom = bottom;
        calls.push_back(call);
    }
    float textWidth(FontFace, float em, std::string_view text) override { return widthOf(em, text); }
    float lineHeight(FontFace, float em) override { return em * 1.5f; }
    void text(float x, float top, float em, FontFace face, std::string_view text, PageColor color) override {
        Call call;
        call.kind = 't';
        call.x = x;
        call.top_ = top;
        call.em = em;
        call.face = face;
        call.text = std::string(text);
        call.top = color;
        calls.push_back(call);
    }
    void clip(const PageRect *clip) override {
        Call call;
        call.kind = clip ? 'c' : 'n';
        if (clip) call.rect = *clip;
        calls.push_back(call);
    }
};

Item quad(float x, float y, float w, float h, uint32_t color) {
    Item item;
    item.x = x;
    item.y = y;
    item.w = w;
    item.h = h;
    item.color = color;
    return item;
}

Item text(float x, float y, float w, const std::string &string, Align align, float size = 20.0f) {
    Item item;
    item.kind = ItemKind::Text;
    item.x = x;
    item.y = y;
    item.w = w;
    item.h = size * 1.25f;
    item.size = size;
    item.align = align;
    item.text = string;
    return item;
}

}  // namespace

int main() {
    // The canvas in the content rectangle: 1080 high scaled to the content's height, centred across.
    {
        for (const auto &[canvasWidth, content] :
             {std::pair{1920.0f, PageRect{0, 0, 3840, 2160}}, std::pair{2520.0f, PageRect{0, 0, 2520, 1080}},
              std::pair{1440.0f, PageRect{240, 0, 1440, 1080}}, std::pair{1920.0f, PageRect{0, 60, 1280, 720}}}) {
            DrawList list;
            list.canvasWidth = canvasWidth;
            list.items.push_back(quad(0, 0, canvasWidth, 1080, 0xFFFFFFFFu));
            list.items.push_back(quad(100, 200, 50, 40, 0x102030FFu));
            Recorder sink;
            PageRenderer renderer;
            renderer.draw(list, content, sink);
            const float scale = content.h / 1080.0f;
            check(sink.calls.size() == 2 && near(sink.calls[0].rect.x, content.x) &&
                      near(sink.calls[0].rect.y, content.y) && near(sink.calls[0].rect.w, content.w) &&
                      near(sink.calls[0].rect.h, content.h),
                  "the whole canvas fills the content rectangle at " + std::to_string(int(canvasWidth)));
            check(near(sink.calls[1].rect.x, content.x + 100 * scale) &&
                      near(sink.calls[1].rect.y, content.y + 200 * scale) && near(sink.calls[1].rect.w, 50 * scale),
                  "an item scales with the content's height");
        }
        // A 16:9 canvas in a 4:3 pillarbox's content rectangle (aspect.ratio 4:3 in a 16:9 window): centred, its
        // sides past the content's (the page engine lays out for the content's own aspect: this only checks the
        // mapping keeps the centre).
        DrawList list;
        list.items.push_back(quad(960, 540, 0, 0, 0xFFFFFFFFu));
        Recorder sink;
        PageRenderer().draw(list, PageRect{240, 0, 1440, 1080}, sink);
        check(sink.calls.size() == 1 && near(sink.calls[0].rect.x, 960) && near(sink.calls[0].rect.y, 540),
              "the canvas's centre is the content's centre");
    }

    // Colour times opacity; a gradient's two colours; a fully transparent item is not drawn.
    {
        check(pageColor(0x11223380u, 0.5f) == PageColor{0x11, 0x22, 0x33, 0x40}, "alpha times opacity");
        check(pageColor(0xFFFFFFFFu, 2.0f).a == 255 && pageColor(0xFFFFFFFFu, -1.0f).a == 0, "opacity clamped");
        DrawList list;
        Item gradient = quad(0, 0, 10, 10, 0x000000FFu);
        gradient.kind = ItemKind::Gradient;
        gradient.color2 = 0xFFFFFF80u;
        list.items.push_back(gradient);
        list.items.push_back(quad(0, 0, 10, 10, 0xFFFFFF00u));
        Recorder sink;
        PageRenderer().draw(list, PageRect{0, 0, 1920, 1080}, sink);
        check(sink.calls.size() == 1 && sink.calls[0].top == PageColor{0, 0, 0, 255} &&
                  sink.calls[0].bottom == PageColor{255, 255, 255, 128},
              "a gradient from colour (top) to colour2 (bottom); a transparent quad skipped");
    }

    // Text: the faces, alignment in the box, no box, and the ellipsis cut.
    {
        check(pageFace(Font::Title, true) == FontFace::Title && pageFace(Font::Title, false) == FontFace::Row &&
                  pageFace(Font::Body, false) == FontFace::Help && pageFace(Font::Accent, false) == FontFace::Accent,
              "the families on the faces (MENU_DESIGN 5.2)");
        DrawList list;
        list.items.push_back(text(100, 50, 200, "abcd", Align::Left));     // 4 x 10 = 40 wide at 1080p
        list.items.push_back(text(100, 50, 200, "abcd", Align::Center));
        list.items.push_back(text(100, 50, 200, "abcd", Align::Right));
        list.items.push_back(text(500, 50, 0, "abcd", Align::Center));     // no box: centred on x
        list.items.push_back(text(500, 50, 0, "abcd", Align::Right));
        list.items.push_back(text(0, 0, 45, "abcdefghij", Align::Left));   // 100 wide in 45: cut
        list.items.push_back(text(0, 0, 45, "ab \xC3\xA9\xC3\xA9\xC3\xA9xyz", Align::Left));
        Item italic = text(0, 0, 0, "x", Align::Left);
        italic.font = Font::Body;
        italic.italic = true;
        list.items.push_back(italic);
        list.items.push_back(italic);
        Recorder sink;
        PageRenderer renderer;
        renderer.draw(list, PageRect{0, 0, 1920, 1080}, sink);
        check(sink.calls.size() == 9, "every text drawn");
        if (sink.calls.size() == 9) {
            check(near(sink.calls[0].x, 100) && near(sink.calls[0].top_, 50) && near(sink.calls[0].em, 20),
                  "left: the box's left, its top, em = size");
            check(near(sink.calls[1].x, 180), "centre: in the box");
            check(near(sink.calls[2].x, 260), "right: at the box's right");
            check(near(sink.calls[3].x, 480) && near(sink.calls[4].x, 460), "no box: centred on x, or ending at x");
            check(sink.calls[5].text == "abc\xE2\x80\xA6" && Recorder::widthOf(20, sink.calls[5].text) <= 45,
                  "a long text cut with an ellipsis, inside its box: " + sink.calls[5].text);
            // 45 px at 10 per code point: 3 code points and the ellipsis; "ab " loses its space.
            check(sink.calls[6].text == "ab\xE2\x80\xA6", "the cut at a code point, a trailing space dropped: " +
                                                              sink.calls[6].text);
            check(sink.calls[7].face == FontFace::Help, "italic Body drawn in its upright face");
        }
        int italicNotes = 0;
        for (const auto &note : renderer.notes()) italicNotes += note.find("italic") != std::string::npos;
        check(italicNotes == 1, "the missing italic noted once");
        std::string cut = fitText(sink, FontFace::Row, 20, "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 25);
        check(cut == "\xC3\xA9\xE2\x80\xA6", "never inside a code point: " + cut);
        check(fitText(sink, FontFace::Row, 20, "abc", 5) == "\xE2\x80\xA6", "nothing fits: the ellipsis alone");
    }

    // Wrapping (the architect's cases): a box two lines high breaks at a space and cuts only its last line; a word
    // longer than the box breaks at a code point; an empty line keeps its height. Lines are 1.5 em (30 px at em 20),
    // 10 px a code point.
    {
        auto drawn = [](const Item &item) {
            DrawList list;
            list.items.push_back(item);
            Recorder sink;
            PageRenderer().draw(list, PageRect{0, 0, 1920, 1080}, sink);
            std::vector<std::pair<float, std::string>> lines;
            for (const auto &call : sink.calls) lines.push_back({call.top_, call.text});
            return lines;
        };
        Item two = text(0, 0, 100, "hello world again", Align::Left);
        two.h = 60;
        auto lines = drawn(two);
        check(lines.size() == 2 && lines[0].second == "hello" && lines[1].second == "world aga\xE2\x80\xA6" &&
                  near(lines[1].first, 30),
              "two lines: a break at the space, the last line cut");
        Item word = text(0, 0, 100, "abcdefghijklmnop", Align::Left);
        word.h = 60;
        lines = drawn(word);
        check(lines.size() == 2 && lines[0].second == "abcdefghij" && lines[1].second == "klmnop",
              "a word longer than the box broken at a code point");
        Item blank = text(0, 0, 100, "ab\n\ncd", Align::Left);
        blank.h = 90;
        lines = drawn(blank);
        check(lines.size() == 2 && lines[0].second == "ab" && lines[1].second == "cd" && near(lines[1].first, 60),
              "an empty line keeps its height");
        Item single = text(0, 0, 100, "hello world again", Align::Left);
        lines = drawn(single);
        check(lines.size() == 1 && lines[0].second == "hello wor\xE2\x80\xA6", "a one-line box: cut, not wrapped");
        Recorder sink;
        const auto spaces = wrapText(sink, FontFace::Row, 20, "hello world   \nnext", 100, 3);
        check(spaces.size() == 3 && spaces[0] == "hello" && spaces[1] == "world" && spaces[2] == "next",
              "a paragraph's trailing spaces make no empty line");
    }

    // Clips: nested ones intersect, a pop restores the outer one, the last pop clears; an unbalanced pop is ignored
    // and noted once; a list that ends with a clip pushed clears it.
    {
        DrawList list;
        Item outer = quad(100, 100, 400, 400, 0);
        outer.kind = ItemKind::ClipPush;
        Item inner = quad(300, 50, 400, 100, 0);
        inner.kind = ItemKind::ClipPush;
        Item pop;
        pop.kind = ItemKind::ClipPop;
        list.items = {outer, inner, pop, pop, pop, pop, outer};
        Recorder sink;
        PageRenderer renderer;
        renderer.draw(list, PageRect{0, 0, 1920, 1080}, sink);
        std::string kinds;
        for (const auto &call : sink.calls) kinds += call.kind;
        check(kinds == "cccncn", "push, push, pop, pop (clear), the pushed clip at the end cleared: " + kinds);
        if (sink.calls.size() == 6) {
            const PageRect &both = sink.calls[1].rect;
            check(near(both.x, 300) && near(both.y, 100) && near(both.w, 200) && near(both.h, 50),
                  "nested clips intersect");
            check(near(sink.calls[2].rect.x, 100) && near(sink.calls[2].rect.w, 400), "a pop restores the outer");
        }
        check(renderer.notes().size() == 1, "the unbalanced pops noted once");
    }

    // Assets: none packed until M10, so any asset ID draws flat in the item's colour, noted once per ID.
    {
        DrawList list;
        Item tile = quad(0, 0, 10, 10, 0x336699FFu);
        tile.asset = assetId("tab.graphics");
        list.items = {tile, tile};
        tile.kind = ItemKind::NineSlice;
        list.items.push_back(tile);
        Recorder sink;
        PageRenderer renderer;
        renderer.draw(list, PageRect{0, 0, 1920, 1080}, sink);
        check(sink.calls.size() == 3 && sink.calls[2].top == PageColor{0x33, 0x66, 0x99, 0xFF},
              "assets drawn flat in their colour");
        check(renderer.notes().size() == 1, "one note per asset ID");
    }

    // The hub M2 lays out (the real page engine, a page with a long help line), drawn whole at 16:9, 21:9 and 4:3:
    // every text inside its box, every clip popped.
    {
        Schema schema;
        Page page;
        page.id = "graphics";
        page.title = "Graphics";
        for (int i = 0; i < 12; ++i) {
            Row row;
            row.kind = RowKind::Choice;
            row.key = "row." + std::to_string(i);
            row.label = "A row with a rather long label number " + std::to_string(i);
            row.choices = {{"off", "Off"}, {"on", "A value long enough to need cutting in its box"}};
            row.value = "on";
            row.help = "Help text that runs on for a while so that the panel has to cut it somewhere near its end.";
            page.rows.push_back(row);
        }
        Page audio;
        audio.id = "audio";
        audio.title = "Audio";
        schema.pages = {page, audio};
        schema.tabs = {"graphics", "audio"};
        Menu menu;
        menu.setSchema(schema);
        menu.open();
        menu.tick(5.0f);   // past the fade-in and the page's slide
        for (const float width : {1920.0f, 2520.0f, 1440.0f}) {
            LayoutOptions options;
            options.canvasWidth = width;
            const DrawList list = layout(menu, options);
            Recorder sink;
            PageRenderer renderer;
            const PageRect content{0, 0, width * 2, 2160};
            renderer.draw(list, content, sink);
            int texts = 0, clipDepth = 0;
            bool inside = true;
            // Each boxed text on its own (a wrapped one draws several lines): every line inside the box's width, and
            // no more lines than its height holds.
            for (const Item &item : list.items) {
                if (item.kind != ItemKind::Text || item.text.empty() || item.w <= 0) continue;
                DrawList one;
                one.canvasWidth = list.canvasWidth;
                one.items.push_back(item);
                Recorder lines;
                PageRenderer().draw(one, content, lines);
                const float lineHeight = item.size * 2 * 1.5f;
                const size_t most = std::max<size_t>(1, static_cast<size_t>(item.h * 2 / lineHeight + 0.01f));
                for (const auto &drawn : lines.calls)
                    if (Recorder::widthOf(drawn.em, drawn.text) > item.w * 2 + 0.01f) {
                        inside = false;
                        std::printf("  outside its box (%.1f units): '%s' drawn as '%s'\n", item.w, item.text.c_str(),
                                    drawn.text.c_str());
                    }
                if (lines.calls.size() > most) {
                    inside = false;
                    std::printf("  %zu lines in a box for %zu: '%s'\n", lines.calls.size(), most, item.text.c_str());
                }
                ++texts;
            }
            for (const auto &c : sink.calls) clipDepth = c.kind == 'c' ? clipDepth + 1 : c.kind == 'n' ? 0 : clipDepth;
            check(texts > 10 && inside, "the hub's texts inside their boxes at canvas width " +
                                            std::to_string(int(width)) + " (" + std::to_string(texts) + " boxed, " +
                                            std::to_string(list.items.size()) + " items)");
            check(clipDepth == 0, "the hub's clips popped");
            // Until the art is packed the layout names no asset (its stand-ins), so the hub draws without a note.
            for (const auto &note : renderer.notes())
                check(kUiAssetsPacked && note.find("asset") != std::string::npos, "a note on the hub: " + note);
        }
    }

    if (failures) {
        std::printf("ui_renderer: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui_renderer: all checks passed\n");
    return 0;
}
