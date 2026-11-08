#include "ui_records.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace gt4::ui {

namespace {

template <class T> void put(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof value);
}

template <class T> bool get(const uint8_t* data, size_t size, size_t& at, T& value) {
    if (size < at || size - at < sizeof value) return false;
    std::memcpy(&value, data + at, sizeof value);
    at += sizeof value;
    return true;
}

std::string escape(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (char c : field) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// One line into its fields, unescaped.
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out(1);
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\t') {
            out.emplace_back();
        } else if (c == '\\' && i + 1 < line.size()) {
            const char next = line[++i];
            out.back() += next == 't' ? '\t' : next == 'n' ? '\n' : next;
        } else {
            out.back() += c;
        }
    }
    return out;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::string join(std::initializer_list<std::string> parts) {
    std::string out;
    bool first = true;
    for (const auto& part : parts) {
        if (!first) out += '\t';
        out += escape(part);
        first = false;
    }
    out += '\n';
    return out;
}

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.9g", static_cast<double>(value));
    return text;
}

bool flag(const std::string& text) { return text == "1"; }

}  // namespace

bool encodeDrawList(const DrawList& list, uint32_t vblank, uint32_t listId, uint32_t dropped, std::vector<uint8_t>& out) {
    out.clear();
    size_t textBytes = 0;
    for (const auto& item : list.items) textBytes += item.text.size();
    const size_t total = sizeof(wire::UiHeader) + list.items.size() * sizeof(wire::UiItem) + textBytes;
    if (total > wire::kUiMaxBytes) return false;
    out.reserve(total);
    wire::UiHeader header{vblank, listId, list.flags, list.canvasWidth, static_cast<uint32_t>(list.items.size()),
                          static_cast<uint32_t>(textBytes), dropped};
    put(out, header);
    uint32_t offset = 0;
    for (const auto& item : list.items) {
        wire::UiItem w{};
        w.kind = static_cast<uint8_t>(item.kind);
        w.font = static_cast<uint8_t>(item.font);
        w.align = static_cast<uint8_t>(item.align);
        w.italic = item.italic ? 1 : 0;
        w.weight = item.weight;
        w.x = item.x;
        w.y = item.y;
        w.w = item.w;
        w.h = item.h;
        w.asset = item.asset;
        w.color = item.color;
        w.color2 = item.color2;
        w.opacity = item.opacity;
        w.size = item.size;
        w.textOffset = offset;
        w.textLength = static_cast<uint32_t>(item.text.size());
        offset += w.textLength;
        put(out, w);
    }
    for (const auto& item : list.items) out.insert(out.end(), item.text.begin(), item.text.end());
    return true;
}

bool decodeDrawList(const uint8_t* data, size_t size, DrawList& list, wire::UiHeader* headerOut) {
    size_t at = 0;
    wire::UiHeader header;
    if (!get(data, size, at, header)) return false;
    const size_t itemsBytes = size_t(header.items) * sizeof(wire::UiItem);
    if (size - at < itemsBytes || size - at - itemsBytes < header.textBytes) return false;
    const uint8_t* text = data + at + itemsBytes;
    list = DrawList();
    list.flags = header.flags;
    list.canvasWidth = header.canvasWidth;
    list.items.reserve(header.items);
    for (uint32_t i = 0; i < header.items; ++i) {
        wire::UiItem w;
        get(data, size, at, w);
        if (w.kind > static_cast<uint8_t>(ItemKind::ClipPop) || w.font > static_cast<uint8_t>(Font::Accent) ||
            w.align > static_cast<uint8_t>(Align::Right))
            return false;
        if (w.textOffset > header.textBytes || header.textBytes - w.textOffset < w.textLength) return false;
        Item item;
        item.kind = static_cast<ItemKind>(w.kind);
        item.font = static_cast<Font>(w.font);
        item.align = static_cast<Align>(w.align);
        item.italic = w.italic != 0;
        item.weight = w.weight;
        item.x = w.x;
        item.y = w.y;
        item.w = w.w;
        item.h = w.h;
        item.asset = w.asset;
        item.color = w.color;
        item.color2 = w.color2;
        item.opacity = w.opacity;
        item.size = w.size;
        item.text.assign(reinterpret_cast<const char*>(text) + w.textOffset, w.textLength);
        list.items.push_back(std::move(item));
    }
    if (headerOut) *headerOut = header;
    return true;
}

std::string encodeSchema(const Schema& schema) {
    std::string out = "tabs";
    for (const auto& tab : schema.tabs) out += "\t" + escape(tab);
    out += '\n';
    for (const auto& page : schema.pages) {
        out += join({"page", page.id, page.title, page.icon, page.livePreview ? "1" : "0"});
        for (const auto& row : page.rows) {
            out += join({"row", std::to_string(static_cast<int>(row.kind)), row.key, row.label, row.help,
                         number(row.minimum), number(row.maximum), number(row.step), row.percent ? "1" : "0", row.unit,
                         std::to_string(static_cast<int>(row.apply)), row.previewFirst ? "1" : "0",
                         row.enabled ? "1" : "0", row.value, row.presetValue, row.unused, row.pending ? "1" : "0"});
            for (const auto& choice : row.choices) out += join({"choice", choice.value, choice.label});
        }
    }
    return out;
}

bool decodeSchema(const std::string& text, Schema& schema) {
    Schema out;
    for (const auto& line : lines(text)) {
        const auto f = fields(line);
        if (f[0] == "tabs") {
            out.tabs.assign(f.begin() + 1, f.end());
        } else if (f[0] == "page") {
            if (f.size() != 5) return false;
            Page page;
            page.id = f[1];
            page.title = f[2];
            page.icon = f[3];
            page.livePreview = flag(f[4]);
            out.pages.push_back(std::move(page));
        } else if (f[0] == "row") {
            if (f.size() != 17 || out.pages.empty()) return false;
            const int kind = std::atoi(f[1].c_str()), apply = std::atoi(f[10].c_str());
            if (kind < 0 || kind > static_cast<int>(RowKind::Link) || apply < 0 ||
                apply > static_cast<int>(ApplyClass::Restart))
                return false;
            Row row;
            row.kind = static_cast<RowKind>(kind);
            row.key = f[2];
            row.label = f[3];
            row.help = f[4];
            row.minimum = std::strtof(f[5].c_str(), nullptr);
            row.maximum = std::strtof(f[6].c_str(), nullptr);
            row.step = std::strtof(f[7].c_str(), nullptr);
            row.percent = flag(f[8]);
            row.unit = f[9];
            row.apply = static_cast<ApplyClass>(apply);
            row.previewFirst = flag(f[11]);
            row.enabled = flag(f[12]);
            row.value = f[13];
            row.presetValue = f[14];
            row.unused = f[15];
            row.pending = flag(f[16]);
            out.pages.back().rows.push_back(std::move(row));
        } else if (f[0] == "choice") {
            if (f.size() != 3 || out.pages.empty() || out.pages.back().rows.empty()) return false;
            out.pages.back().rows.back().choices.push_back(ChoiceValue{f[1], f[2]});
        }
        // other line types: a newer host's, skipped
    }
    schema = std::move(out);
    return true;
}

std::string encodeValues(const std::vector<ValueUpdate>& values) {
    std::string out;
    for (const auto& v : values) out += join({"value", v.key, v.value, v.unused, v.pending ? "1" : "0"});
    return out;
}

bool decodeValues(const std::string& text, std::vector<ValueUpdate>& values) {
    std::vector<ValueUpdate> out;
    for (const auto& line : lines(text)) {
        const auto f = fields(line);
        if (f[0] != "value") continue;
        if (f.size() != 5) return false;
        out.push_back(ValueUpdate{f[1], f[2], f[3], flag(f[4])});
    }
    values = std::move(out);
    return true;
}

std::vector<uint8_t> encodeSettings(uint32_t kind, uint32_t request, bool ok, const std::string& body) {
    std::vector<uint8_t> out;
    put(out, wire::SettingsHeader{kind, request, ok ? 1u : 0u, static_cast<uint32_t>(body.size())});
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool decodeSettings(const uint8_t* data, size_t size, wire::SettingsHeader& header, std::string& body) {
    size_t at = 0;
    if (!get(data, size, at, header) || size - at < header.bytes || header.kind > wire::kResult) return false;
    body.assign(reinterpret_cast<const char*>(data) + at, header.bytes);
    return true;
}

std::vector<uint8_t> encodeSet(uint32_t request, uint32_t kind, const std::string& key, const std::string& value) {
    std::vector<uint8_t> out;
    put(out, wire::SetHeader{request, kind, static_cast<uint32_t>(key.size()), static_cast<uint32_t>(value.size())});
    out.insert(out.end(), key.begin(), key.end());
    out.insert(out.end(), value.begin(), value.end());
    return out;
}

bool decodeSet(const uint8_t* data, size_t size, wire::SetHeader& header, std::string& key, std::string& value) {
    size_t at = 0;
    if (!get(data, size, at, header) || header.kind > wire::kOpenSchema) return false;
    if (size - at < header.keyBytes || size - at - header.keyBytes < header.valueBytes) return false;
    key.assign(reinterpret_cast<const char*>(data) + at, header.keyBytes);
    value.assign(reinterpret_cast<const char*>(data) + at + header.keyBytes, header.valueBytes);
    return true;
}

}  // namespace gt4::ui
