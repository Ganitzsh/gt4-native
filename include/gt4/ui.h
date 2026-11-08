#pragma once
// UI records on the link (docs/modern/MENU_DESIGN.md 3.6). Game to host: GT4_REC_UI, the settings pages' draw list
// once a game frame while a page is open (an empty list at close); GT4_REC_SETTINGS_SET, a page's request to set,
// revert or act. Host to game: GT4_REC_SETTINGS, the schema and values, and each request's result.
// Little-endian, 4-byte fields, no 8-byte members: the same layout in the 32-bit game and the 64-bit host
// (tests/ui_records.cpp checks sizes and offsets). Encoders and decoders: platform/ui/ui_records.h.
#include <cstddef>
#include <cstdint>

namespace gt4::ui::wire {

// A UI record is at most this long; the game drops a longer list and counts it (kUiDropped in the next header).
constexpr uint32_t kUiMaxBytes = 64u * 1024u;

// GT4_REC_UI: UiHeader, then `items` UiItems, then `textBytes` of UTF-8 text the items point into.
struct UiHeader {
    uint32_t vblank;        // the game frame the list belongs to (drawn with that frame's FrameList)
    uint32_t list;          // counts up per list sent
    uint32_t flags;         // ui::DrawFlags
    float canvasWidth;      // canvas units; the canvas is 1080 high
    uint32_t items;
    uint32_t textBytes;
    uint32_t dropped;       // lists the game dropped since the last one sent (over kUiMaxBytes)
};
static_assert(sizeof(UiHeader) == 28, "UiHeader layout");

struct UiItem {
    uint8_t kind, font, align, italic;   // ui::ItemKind, ui::Font, ui::Align, 0 / 1
    uint16_t weight, reserved;
    float x, y, w, h;
    uint32_t asset, color, color2;
    float opacity, size;
    uint32_t textOffset, textLength;     // into the text block
};
static_assert(sizeof(UiItem) == 52, "UiItem layout");
static_assert(offsetof(UiItem, x) == 8 && offsetof(UiItem, asset) == 24 && offsetof(UiItem, opacity) == 36 &&
                  offsetof(UiItem, textOffset) == 44,
              "UiItem offsets");

// GT4_REC_SETTINGS_SET: SetHeader, then the key, then the value (UTF-8, no terminators).
enum SetKind : uint32_t { kSet = 0, kRevert = 1, kAction = 2, kOpenSchema = 3 };   // 3: send me the schema
struct SetHeader {
    uint32_t request;       // counts up per request; the host's GT4_REC_SETTINGS result carries it back
    uint32_t kind;          // SetKind
    uint32_t keyBytes, valueBytes;
};
static_assert(sizeof(SetHeader) == 16, "SetHeader layout");

// GT4_REC_SETTINGS: SettingsHeader, then `bytes` of body (platform/ui/ui_records.h's text forms).
enum SettingsKind : uint32_t {
    kSchema = 0,   // body: the whole schema (pages, rows, values)
    kValues = 1,   // body: values that changed (by a request or anything else)
    kResult = 2,   // body: empty when ok, else why the request was refused
};
struct SettingsHeader {
    uint32_t kind;          // SettingsKind
    uint32_t request;       // kResult: the request it answers; else 0
    uint32_t ok;            // kResult: 1 when applied
    uint32_t bytes;
};
static_assert(sizeof(SettingsHeader) == 16, "SettingsHeader layout");

}  // namespace gt4::ui::wire
