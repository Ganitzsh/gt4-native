#include "menu_layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace gt4::ui {

namespace {

using namespace palette;

constexpr float kMargin = 80.0f;
constexpr float kTopBar = 112.0f;
constexpr float kBottomBar = 108.0f;
constexpr float kRowHeight = 58.0f;
constexpr float kListTop = 300.0f;
constexpr float kPanelWidth = 620.0f;
constexpr float kPanelGap = 40.0f;
constexpr float kListMax = 1080.0f;

struct Builder {
    DrawList list;
    float opacity = 1.0f;   // the whole menu's fade
    float dx = 0.0f;        // the page's slide

    // Positions land on half canvas units and opacities on 1/256: exact in binary, so the list is the same on every
    // compiler (clang contracts a * b + c into an FMA, MSVC x64 does not) and whole pixels at 1080p and 2160p.
    static float half(float v) { return std::round(v * 2.0f) * 0.5f; }
    static float alpha256(float v) { return std::round(std::clamp(v, 0.0f, 1.0f) * 256.0f) / 256.0f; }

    Item& add(ItemKind kind, float x, float y, float w, float h, uint32_t color, float alpha = 1.0f) {
        Item item;
        item.kind = kind;
        item.x = half(x);
        item.y = half(y);
        item.w = half(w);
        item.h = half(h);
        item.color = color;
        item.opacity = alpha256(alpha * opacity);
        list.items.push_back(std::move(item));
        return list.items.back();
    }
    // An asset's ID, or 0 (drawn flat in the item's colour) until the art is packed: no item names an asset the
    // host does not have.
    static uint32_t art(const char* asset) { return asset && kUiAssetsPacked ? assetId(asset) : 0; }
    Item& quad(float x, float y, float w, float h, uint32_t color, float alpha = 1.0f, const char* asset = nullptr) {
        Item& item = add(ItemKind::Quad, x, y, w, h, color, alpha);
        item.asset = art(asset);
        return item;
    }
    Item& text(float x, float y, float w, const std::string& text, Font font, float size, uint32_t color,
               Align align = Align::Left, bool italic = false, uint16_t weight = 400, float alpha = 1.0f) {
        Item& item = add(ItemKind::Text, x, y, w, size * 1.25f, color, alpha);
        item.font = font;
        item.size = size;
        item.italic = italic;
        item.weight = weight;
        item.align = align;
        item.text = text;
        return item;
    }
};

std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

float ramp(float time, float length) { return length <= 0.0f ? 1.0f : std::clamp(time / length, 0.0f, 1.0f); }

std::string valueLabel(const Menu& menu, const Row& row) {
    const std::string value = menu.shownValue(row);
    if (row.kind == RowKind::Choice) {
        for (const auto& choice : row.choices)
            if (choice.value == value) return choice.label;
        return value;
    }
    if (row.kind == RowKind::Step) return formatStep(row, value);
    return std::string();
}

const char* applyLine(const Row& row) {
    switch (row.apply) {
    case ApplyClass::Live: return "Applies at once.";
    case ApplyClass::Confirm: return "Applies at once; you are asked to keep it.";
    case ApplyClass::NextRace: return "Applies from the next race.";
    case ApplyClass::Restart: return "Applies when the game starts again.";
    }
    return "";
}

// The prompt glyph's asset ("prompt.xbox.confirm"): our own drawings per controller (MENU_DESIGN 5.3).
std::string prompt(PromptStyle style, const char* button) {
    const char* family = style == PromptStyle::Xbox ? "xbox" : style == PromptStyle::PlayStation ? "ps" : "kb";
    return std::string("prompt.") + family + "." + button;
}

// Text is measured as 0.7 em a character (Saira Semi Expanded's advances run wider than 0.55; only spacing and the
// boxes' widths depend on it, the host measures the real text).
constexpr float kEmEstimate = 0.7f;

// A key cap (no art yet, kUiAssetsPacked): a flat cap 36 high with the button's name; returns its width.
float keyCapWidth(const char* name) {
    return std::max(36.0f, static_cast<float>(std::string(name).size()) * 18.0f * kEmEstimate + 20.0f);
}
void keyCap(Builder& b, float x, float y, const char* name) {
    const float w = keyCapWidth(name);
    b.quad(x, y, w, 36.0f, kLine);
    b.text(x, y + 6.0f, w, name, Font::Title, 18.0f, kText, Align::Center, false, 600);
}

// Returns the left edge of the first entry (the help line ends before it).
float prompts(Builder& b, float canvasWidth, PromptStyle style,
              const std::vector<std::pair<const char*, std::string>>& entries) {
    // Right-aligned in the help bar: glyph (36 x 36, or a key cap) then its label, 40 units between entries.
    float x = canvasWidth - kMargin;
    const float y = kCanvasHeight - kBottomBar + 36.0f;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const float labelWidth = static_cast<float>(it->second.size()) * 20.0f * kEmEstimate + 8.0f;
        x -= labelWidth;
        b.text(x, y + 6.0f, labelWidth, it->second, Font::Title, 20.0f, kText, Align::Left, false, 600);
        x -= 8.0f;
        if (kUiAssetsPacked) {
            x -= 36.0f;
            b.quad(x, y, 36.0f, 36.0f, 0xFFFFFFFFu, 1.0f, prompt(style, it->first).c_str());
        } else {
            const char* name = buttonName(style, it->first);
            x -= keyCapWidth(name);
            keyCap(b, x, y, name);
        }
        x -= 40.0f;
    }
    return x + 40.0f;
}

}  // namespace

const char* buttonName(PromptStyle style, const char* button) {
    // The page engine's buttons: confirm Cross, back Circle, reset Square, tabs L1 / R1 (the keyboard's default keys
    // for them: input_map.cpp).
    struct Names { const char *button, *xbox, *ps, *keyboard; };
    static const Names kNames[] = {
        {"confirm", "A", "Cross", "X"},  {"back", "B", "Circle", "C"},  {"reset", "X", "Square", "Z"},
        {"tabs", "LB RB", "L1 R1", "Q E"}, {"tab_left", "LB", "L1", "Q"}, {"tab_right", "RB", "R1", "E"},
    };
    for (const Names& n : kNames)
        if (std::string(button) == n.button)
            return style == PromptStyle::Xbox ? n.xbox : style == PromptStyle::PlayStation ? n.ps : n.keyboard;
    return "?";
}

DrawList layout(const Menu& menu, const LayoutOptions& options) {
    Builder b;
    const float W = options.canvasWidth;
    b.list.canvasWidth = W;
    b.list.flags = kDimGame;
    if (!menu.isOpen()) return b.list;
    const Page* page = menu.page();
    b.opacity = ramp(menu.openTime(), menu.transition());

    // The game's picture under the menu, dimmed; less on pages whose changes show on it (MENU_DESIGN 4.3).
    b.quad(0.0f, 0.0f, W, kCanvasHeight, 0x000000FFu, page && page->livePreview ? 0.15f : 0.6f);

    // Top bar: title, and the preset chip.
    b.quad(0.0f, 0.0f, W, kTopBar, kBar);
    b.text(kMargin, 22.0f, 0.0f, menu.inSubPage() && page ? page->title : "Settings", Font::Title, 64.0f, kText,
           Align::Left, true, 800);

    // Tabs: icon tiles on plinths, the selected one framed in the accent with its name below. Until the art is packed:
    // text chips, the selected one in the accent, the tab keys' caps either side.
    const auto& tabs = menu.schema().tabs;
    const float tile = 108.0f, gap = 20.0f;
    const float tabsX = kMargin;
    if (!kUiAssetsPacked) {
        // The chips at 24 units, or smaller so the row and the right tab key's cap fit the canvas.
        std::vector<std::string> titles;
        float letters = 0.0f;
        for (const auto& id : tabs) {
            std::string title = id;
            for (const auto& p : menu.schema().pages)
                if (p.id == id) title = p.title;
            letters += static_cast<float>(title.size());
            titles.push_back(title);
        }
        const float room = W - kMargin - tabsX - keyCapWidth(buttonName(options.prompts, "tab_right")) - 16.0f;
        const float fixed = static_cast<float>(tabs.size()) * 36.0f + static_cast<float>(tabs.size() - 1) * 12.0f;
        const float size = letters > 0.0f
            ? std::clamp((room - fixed) / (letters * kEmEstimate), 16.0f, 24.0f) : 24.0f;
        float x = tabsX;
        for (size_t i = 0; i < tabs.size(); ++i) {
            const bool selected = static_cast<int>(i) == menu.tab();
            const float w = static_cast<float>(titles[i].size()) * size * kEmEstimate + 36.0f;
            b.quad(x, 150.0f, w, 56.0f, selected ? kAccent : kPanel);
            b.text(x, 178.0f - size * 0.625f, w, titles[i], Font::Title, size, selected ? kInk : kSecondary,
                   Align::Center, false, 700);
            x += w + 12.0f;
        }
        if (tabs.size() > 1) {
            const char* left = buttonName(options.prompts, "tab_left");
            keyCap(b, tabsX - 16.0f - keyCapWidth(left), 160.0f, left);
            keyCap(b, x + 4.0f, 160.0f, buttonName(options.prompts, "tab_right"));
        }
    }
    for (size_t i = 0; kUiAssetsPacked && i < tabs.size(); ++i) {
        const float x = tabsX + static_cast<float>(i) * (tile + gap);
        const bool selected = static_cast<int>(i) == menu.tab();
        Item& plinth = b.add(ItemKind::NineSlice, x, 138.0f, tile, 96.0f, 0xFFFFFFFFu);
        plinth.asset = Builder::art("tab.plinth");
        std::string icon = "tab.unknown";
        for (const auto& p : menu.schema().pages)
            if (p.id == tabs[i] && !p.icon.empty()) icon = p.icon;
        b.quad(x + 22.0f, 146.0f, 64.0f, 64.0f, 0xFFFFFFFFu, 1.0f, icon.c_str());
        if (selected) {
            Item& frame = b.add(ItemKind::NineSlice, x - 6.0f, 132.0f, tile + 12.0f, 108.0f, kAccent);
            frame.asset = Builder::art("tab.selected");
            for (const auto& p : menu.schema().pages)
                if (p.id == tabs[i]) b.text(x + tile * 0.5f - 100.0f, 246.0f, 200.0f, p.title, Font::Title, 22.0f,
                                            kAccent, Align::Center, true, 700);
        }
    }
    if (kUiAssetsPacked && tabs.size() > 1) {
        b.quad(tabsX - 60.0f, 168.0f, 40.0f, 36.0f, 0xFFFFFFFFu, 1.0f, prompt(options.prompts, "tab_left").c_str());
        b.quad(tabsX + static_cast<float>(tabs.size()) * (tile + gap), 168.0f, 40.0f, 36.0f, 0xFFFFFFFFu, 1.0f,
               prompt(options.prompts, "tab_right").c_str());
    }

    // The page's content fades and slides in when the page or tab changes.
    const float pageIn = ramp(menu.pageTime(), menu.transition());
    const float content = b.opacity;
    b.opacity = content * pageIn;
    b.dx = (1.0f - pageIn) * 24.0f;

    const bool panel = W >= 2.0f * kMargin + 900.0f + kPanelGap + kPanelWidth;
    const float listX = kMargin + b.dx;
    const float listW = panel ? std::min(kListMax, W - 2.0f * kMargin - kPanelGap - kPanelWidth) : W - 2.0f * kMargin;
    const Row* selectedRow = nullptr;
    if (page) {
        const int first = menu.scroll();
        const int last = std::min(static_cast<int>(page->rows.size()), first + Menu::kVisibleRows);
        b.add(ItemKind::ClipPush, listX, kListTop - 20.0f, listW, Menu::kVisibleRows * kRowHeight + 20.0f, 0);
        float y = kListTop;
        int band = 0;
        for (int i = first; i < last; ++i) {
            const Row& row = page->rows[static_cast<size_t>(i)];
            if (row.kind == RowKind::Header) {
                b.text(listX, y + 8.0f, listW, upper(row.label), Font::Title, 26.0f, kAccent, Align::Left, true, 700);
                y += kRowHeight;
                band = 0;
                continue;
            }
            const bool selected = i == menu.row();
            if (selected) selectedRow = &row;
            const float dim = row.unused.empty() && row.enabled ? 1.0f : 0.45f;
            b.quad(listX, y, listW, kRowHeight, 0xFFFFFFFFu, selected ? 0.18f : (band % 2 ? 0.10f : 0.06f),
                   "row.band");
            ++band;
            const uint32_t color = selected ? kAccent : kText;
            if (selected) b.quad(listX + 8.0f, y + 18.0f, 16.0f, 22.0f, kAccent, 1.0f, "cursor");
            b.text(listX + 40.0f, y + 10.0f, listW * 0.5f, row.label, Font::Title, 30.0f, color, Align::Left, false,
                   500, dim);
            std::string value = valueLabel(menu, row);
            if (row.kind == RowKind::Link) value = "\xE2\x80\xBA";   // a single right-pointing angle: opens a page
            if (selected && (row.kind == RowKind::Choice || row.kind == RowKind::Step) && row.enabled)
                value = "\xE2\x80\xB9  " + value + "  \xE2\x80\xBA";
            const bool slider = row.kind == RowKind::Step && row.maximum > row.minimum;
            if (slider) {
                // The track left of the value's own 200-unit box, so "<  100 %  >" never runs over it.
                const float t = std::clamp((std::strtof(menu.shownValue(row).c_str(), nullptr) - row.minimum) /
                                           (row.maximum - row.minimum), 0.0f, 1.0f);
                const float trackX = listX + listW - 520.0f, trackY = y + 26.0f;
                b.quad(trackX, trackY, 260.0f, 6.0f, kLine, dim);
                b.quad(trackX, trackY, 260.0f * t, 6.0f, kAccent, dim);
                b.quad(trackX + 260.0f * t - 6.0f, trackY - 8.0f, 12.0f, 22.0f, kText, dim, "slider.knob");
            }
            if (row.pending && (row.apply == ApplyClass::NextRace || row.apply == ApplyClass::Restart)) {
                const char* badge = row.apply == ApplyClass::NextRace ? "NEXT RACE" : "RESTART";
                b.quad(listX + listW * 0.52f, y + 13.0f, 150.0f, 32.0f, kChip);
                b.text(listX + listW * 0.52f, y + 17.0f, 150.0f, badge, Font::Title, 20.0f, kInk, Align::Center, false,
                       600);
            }
            if (!value.empty()) {
                const float boxX = slider ? listX + listW - 230.0f : listX + listW * 0.5f;
                b.text(boxX, y + 10.0f, listX + listW - 30.0f - boxX, value, Font::Title, 30.0f,
                       selected ? kAccent : kSecondary, Align::Right, false, 500, dim);
            }
            y += kRowHeight;
        }
        b.add(ItemKind::ClipPop, 0.0f, 0.0f, 0.0f, 0.0f, 0);
        // More rows above or below: small marks at the list's edge.
        if (first > 0) b.quad(listX + listW - 24.0f, kListTop - 18.0f, 16.0f, 10.0f, kSecondary, 1.0f, "scroll.up");
        if (last < static_cast<int>(page->rows.size()))
            b.quad(listX + listW - 24.0f, kListTop + Menu::kVisibleRows * kRowHeight + 6.0f, 16.0f, 10.0f, kSecondary,
                   1.0f, "scroll.down");
    }

    // The detail panel: the selected row's name, help, why it is unused, and when it applies.
    if (panel && selectedRow) {
        const float px = W - kMargin - kPanelWidth + b.dx;
        Item& frame = b.add(ItemKind::NineSlice, px, 320.0f, kPanelWidth, 620.0f, kPanel);
        frame.asset = Builder::art("panel.detail");
        b.quad(px, 320.0f, kPanelWidth, 4.0f, kAccent);
        b.text(px + 30.0f, 350.0f, kPanelWidth - 60.0f, upper(selectedRow->label), Font::Title, 26.0f, kText,
               Align::Left, true, 700);
        // The help wraps in the panel down to the unused line's place (the host wraps: draw_list.h).
        b.text(px + 30.0f, 400.0f, kPanelWidth - 60.0f, selectedRow->help, Font::Body, 24.0f, kSecondary).h = 340.0f;
        float y = 760.0f;
        if (!selectedRow->unused.empty()) {
            b.text(px + 30.0f, y, kPanelWidth - 60.0f, selectedRow->unused, Font::Body, 24.0f, kWarning);
            y += 70.0f;
        }
        if (selectedRow->kind == RowKind::Choice || selectedRow->kind == RowKind::Step)
            b.text(px + 30.0f, y, kPanelWidth - 60.0f,
                   menu.previewing() ? "Confirm to apply, Back to keep the current value." : applyLine(*selectedRow),
                   Font::Body, 24.0f, kSecondary);
    }

    // Help bar: the selected row's help (or why it is unused) and the controller's prompts.
    b.opacity = content;
    b.dx = 0.0f;
    b.quad(0.0f, kCanvasHeight - kBottomBar, W, kBottomBar, kBar);
    std::vector<std::pair<const char*, std::string>> entries;
    if (selectedRow && selectedRow->kind == RowKind::Link) entries.push_back({"confirm", "Open"});
    else if (selectedRow && selectedRow->kind == RowKind::Action) entries.push_back({"confirm", "Select"});
    else if (selectedRow) entries.push_back({"confirm", menu.previewing() ? "Apply" : "Change"});
    entries.push_back({"back", menu.inSubPage() ? "Back" : "Close"});
    if (selectedRow && !selectedRow->presetValue.empty()) entries.push_back({"reset", "Reset"});
    if (menu.schema().tabs.size() > 1) entries.push_back({"tabs", "Tabs"});
    const float promptsLeft = prompts(b, W, options.prompts, entries);
    if (selectedRow) {
        // The help line up to the prompts (40 units short of them), cut by the host when longer.
        const std::string& help = !selectedRow->unused.empty() && !panel ? selectedRow->unused : selectedRow->help;
        b.text(kMargin, kCanvasHeight - kBottomBar + 34.0f, std::max(0.0f, promptsLeft - 40.0f - kMargin), help,
               Font::Body, 28.0f, kText);
    }

    // The keep-or-revert dialog over everything.
    if (menu.confirming()) {
        const float dw = 760.0f, dh = 280.0f, dx = (W - dw) * 0.5f, dy = (kCanvasHeight - dh) * 0.5f;
        b.quad(0.0f, 0.0f, W, kCanvasHeight, 0x000000FFu, 0.5f);
        Item& frame = b.add(ItemKind::NineSlice, dx, dy, dw, dh, kPanel);
        frame.asset = Builder::art("dialog.frame");
        b.quad(dx, dy, dw, 4.0f, kAccent);
        b.text(dx, dy + 50.0f, dw, "Keep these settings?", Font::Title, 36.0f, kText, Align::Center, true, 700);
        char left[64];
        std::snprintf(left, sizeof left, "Going back to the previous ones in %d s",
                      static_cast<int>(std::ceil(std::max(0.0f, menu.confirmLeft()))));
        b.text(dx, dy + 120.0f, dw, left, Font::Body, 26.0f, kSecondary, Align::Center);
        b.quad(dx + dw * 0.5f - 170.0f, dy + 196.0f, 36.0f, 36.0f, 0xFFFFFFFFu, 1.0f,
               prompt(options.prompts, "confirm").c_str());
        b.text(dx + dw * 0.5f - 126.0f, dy + 202.0f, 120.0f, "Keep", Font::Title, 22.0f, kText, Align::Left, false,
               600);
        b.quad(dx + dw * 0.5f + 30.0f, dy + 196.0f, 36.0f, 36.0f, 0xFFFFFFFFu, 1.0f,
               prompt(options.prompts, "back").c_str());
        b.text(dx + dw * 0.5f + 74.0f, dy + 202.0f, 140.0f, "Go back", Font::Title, 22.0f, kText, Align::Left, false,
               600);
    }
    return b.list;
}

}  // namespace gt4::ui
