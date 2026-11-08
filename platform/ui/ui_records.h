#pragma once
// Encoders and decoders of the UI records (include/gt4/ui.h; docs/modern/MENU_DESIGN.md 3.6), the same code in the
// game's runtime and in the host. The draw list is binary (wire::UiHeader, UiItems, text). The schema and values are
// text: one record per line, fields separated by tabs, with \\, \t and \n escaped, so a value can hold any byte but
// NUL. Unknown line types are skipped (a newer host's extra lines), malformed known ones refuse the whole body.
#include "draw_list.h"
#include "menu_model.h"

#include <gt4/ui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gt4::ui {

// GT4_REC_UI. Returns false, leaving `out` empty, when the record would exceed wire::kUiMaxBytes.
bool encodeDrawList(const DrawList& list, uint32_t vblank, uint32_t listId, uint32_t dropped, std::vector<uint8_t>& out);
bool decodeDrawList(const uint8_t* data, size_t size, DrawList& list, wire::UiHeader* header = nullptr);

// One value the host reports: the row's value, why it is unused (empty when used), whether it waits to take effect.
struct ValueUpdate {
    std::string key, value, unused;
    bool pending = false;
};

std::string encodeSchema(const Schema& schema);
bool decodeSchema(const std::string& text, Schema& schema);
std::string encodeValues(const std::vector<ValueUpdate>& values);
bool decodeValues(const std::string& text, std::vector<ValueUpdate>& values);

// GT4_REC_SETTINGS.
std::vector<uint8_t> encodeSettings(uint32_t kind, uint32_t request, bool ok, const std::string& body);
bool decodeSettings(const uint8_t* data, size_t size, wire::SettingsHeader& header, std::string& body);

// GT4_REC_SETTINGS_SET.
std::vector<uint8_t> encodeSet(uint32_t request, uint32_t kind, const std::string& key, const std::string& value);
bool decodeSet(const uint8_t* data, size_t size, wire::SetHeader& header, std::string& key, std::string& value);

}  // namespace gt4::ui
