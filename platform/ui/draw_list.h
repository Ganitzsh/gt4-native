#pragma once
// The UI draw list (docs/modern/MENU_DESIGN.md 3.6): what a settings page asks the host to draw this frame. The
// game's page engine fills it; the host draws it at output resolution with its own images and SDF fonts. Positions
// are canvas units: the canvas is 1080 units high and as wide as the content's aspect (1920 at 16:9), y down.
// Plain C++17, no I/O: built into the game's runtime and into the host.
#include <cstdint>
#include <string>
#include <vector>

namespace gt4::ui {

constexpr float kCanvasHeight = 1080.0f;

// An asset's ID: FNV-1a of its manifest name ("tab.graphics"), so assets/ui/manifest.json and the code agree
// without a shared table. 0 is "no asset" (a flat quad in the item's colour).
constexpr uint32_t assetId(const char* name) {
    uint32_t hash = 2166136261u;
    for (; *name; ++name) hash = (hash ^ static_cast<uint8_t>(*name)) * 16777619u;
    return hash;
}

enum class ItemKind : uint8_t {
    Quad,       // a rectangle: the asset stretched over it, or the colour
    NineSlice,  // the asset as a nine-slice frame (insets from the manifest)
    Gradient,   // a vertical gradient from colour (top) to colour2 (bottom)
    Text,       // UTF-8 text in the rectangle's box: x, y the top-left of the box, w the most it may take (0: no limit)
    ClipPush,   // later items are clipped to the rectangle until the matching ClipPop
    ClipPop,
};
enum class Font : uint8_t { Title, Body, Accent };   // MENU_DESIGN 5.1: wide grotesque, its text cut, the serif
enum class Align : uint8_t { Left, Center, Right };

struct Item {
    ItemKind kind = ItemKind::Quad;
    float x = 0, y = 0, w = 0, h = 0;
    uint32_t asset = 0;
    uint32_t color = 0xFFFFFFFFu;   // 0xRRGGBBAA, multiplied with the asset
    uint32_t color2 = 0;            // Gradient's bottom colour
    float opacity = 1.0f;           // times the colour's alpha
    // Text only
    Font font = Font::Body;
    float size = 30.0f;             // canvas units, the font's em
    bool italic = false;
    uint16_t weight = 400;
    Align align = Align::Left;
    std::string text;
};

enum DrawFlags : uint32_t {
    kDimGame = 1,     // the host darkens the game's frame under the list (by the first item, a full-canvas quad)
    kBlockHud = 2,    // the page covers the screen: the host may skip drawing what lies under it
};

struct DrawList {
    uint32_t flags = 0;
    float canvasWidth = 1920.0f;
    std::vector<Item> items;
};

// One line per item, fixed precision: the golden form of the tests (tests/ui_menu.cpp) and logs.
std::string describe(const DrawList& list);

}  // namespace gt4::ui
