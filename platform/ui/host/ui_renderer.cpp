#include "ui_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gt4::ui {

namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";   // U+2026

PageRect intersect(const PageRect &a, const PageRect &b) {
    const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

std::string hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "%08X", value);
    return text;
}

}  // namespace

FontFace pageFace(Font font, bool italic) {
    switch (font) {
    case Font::Title: return italic ? FontFace::Title : FontFace::Row;
    case Font::Body: return FontFace::Help;
    case Font::Accent: return FontFace::Accent;
    }
    return FontFace::Row;
}

PageColor pageColor(uint32_t rgba, float opacity) {
    const float alpha = static_cast<float>(rgba & 0xFFu) * std::clamp(opacity, 0.0f, 1.0f);
    return {static_cast<uint8_t>(rgba >> 24), static_cast<uint8_t>(rgba >> 16), static_cast<uint8_t>(rgba >> 8),
            static_cast<uint8_t>(std::lround(alpha))};
}

std::string fitText(PageSink &sink, FontFace face, float em, std::string_view text, float width) {
    if (width <= 0.0f || sink.textWidth(face, em, text) <= width) return std::string(text);
    // Code-point boundaries (a byte that is not a continuation byte 10xxxxxx), then the longest prefix that fits
    // with the ellipsis: a binary search, the width growing with the prefix.
    std::vector<size_t> cuts;
    for (size_t i = 1; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) cuts.push_back(i);
    size_t lo = 0, hi = cuts.size();   // the answer is cuts[n - 1] for n in [0, hi]; n = 0 is the ellipsis alone
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        std::string candidate(text.substr(0, cuts[mid - 1]));
        candidate += kEllipsis;
        if (sink.textWidth(face, em, candidate) <= width) lo = mid;
        else hi = mid - 1;
    }
    std::string out(text.substr(0, lo ? cuts[lo - 1] : 0));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + std::string(kEllipsis);
}

std::vector<std::string> wrapText(PageSink &sink, FontFace face, float em, std::string_view text, float width,
                                  int maxLines) {
    std::vector<std::string> lines;
    if (maxLines < 1) maxLines = 1;
    if (width <= 0.0f) {
        lines.emplace_back(text.substr(0, text.find('\n')));
        return lines;
    }
    size_t at = 0;
    while (at <= text.size() && static_cast<int>(lines.size()) < maxLines) {
        const size_t newline = text.find('\n', at);
        const std::string_view rest = text.substr(at, newline == std::string_view::npos ? std::string_view::npos
                                                                                         : newline - at);
        if (static_cast<int>(lines.size()) == maxLines - 1) {
            // The last line: the rest of the text (its following paragraphs too), cut with an ellipsis if need be.
            std::string tail(text.substr(at));
            for (char &c : tail)
                if (c == '\n') c = ' ';
            lines.push_back(fitText(sink, face, em, tail, width));
            return lines;
        }
        // The longest prefix of this paragraph that fits, ending at a space; a first word wider than the line is
        // broken at a code point.
        size_t take = rest.size();
        if (sink.textWidth(face, em, rest) > width) {
            take = 0;
            for (size_t space = rest.find(' '); space != std::string_view::npos; space = rest.find(' ', space + 1)) {
                if (sink.textWidth(face, em, rest.substr(0, space)) > width) break;
                take = space;
            }
            if (take == 0) {
                for (size_t i = 1; i <= rest.size(); ++i) {
                    if (i < rest.size() && (static_cast<unsigned char>(rest[i]) & 0xC0) == 0x80) continue;
                    if (sink.textWidth(face, em, rest.substr(0, i)) > width) break;
                    take = i;
                }
                if (take == 0) take = 1;   // not even one code point fits: one anyway, so the text advances
                while (take < rest.size() && (static_cast<unsigned char>(rest[take]) & 0xC0) == 0x80) ++take;
            }
        }
        std::string_view shown = rest.substr(0, take);
        while (!shown.empty() && shown.back() == ' ') shown.remove_suffix(1);   // (alignment ignores them)
        lines.emplace_back(shown);
        at += take;
        while (at < text.size() && text[at] == ' ') ++at;   // the break's spaces
        if (take < rest.size() && at < text.size() && text[at] == '\n') {
            ++at;   // the break fell on the paragraph's trailing spaces: its newline is that break
            continue;
        }
        if (take == rest.size()) {
            if (newline == std::string_view::npos) break;
            at = newline + 1;
        }
    }
    return lines;
}

void PageRenderer::note(const std::string &text) {
    if (!noted_.insert(text).second) return;
    notes_.push_back(text);
    std::printf("UI page: %s\n", text.c_str());
}

void PageRenderer::draw(const DrawList &list, const PageRect &content, PageSink &sink) {
    if (list.items.empty() || content.w <= 0.0f || content.h <= 0.0f) return;
    const float scale = content.h / kCanvasHeight;
    const float originX = content.x + (content.w - list.canvasWidth * scale) * 0.5f, originY = content.y;
    auto toPixels = [&](const Item &item) {
        return PageRect{originX + item.x * scale, originY + item.y * scale, item.w * scale, item.h * scale};
    };
    std::vector<PageRect> clips;
    for (const Item &item : list.items) {
        switch (item.kind) {
        case ItemKind::Quad:
        case ItemKind::NineSlice:
        case ItemKind::Gradient: {
            if (item.asset) note("asset " + hex(item.asset) + " is not packed yet (M10): drawn flat");
            const PageColor top = pageColor(item.color, item.opacity);
            const PageColor bottom = item.kind == ItemKind::Gradient ? pageColor(item.color2, item.opacity) : top;
            if (top.a == 0 && bottom.a == 0) break;
            sink.rect(toPixels(item), top, bottom);
            break;
        }
        case ItemKind::Text: {
            if (item.text.empty()) break;
            const FontFace face = pageFace(item.font, item.italic);
            if (item.italic && item.font != Font::Title)
                note(std::string(item.font == Font::Body ? "Body" : "Accent") + " italic has no face: drawn upright");
            const PageColor color = pageColor(item.color, item.opacity);
            if (color.a == 0) break;
            const PageRect box = toPixels(item);
            const float em = item.size * scale;
            const float line = sink.lineHeight(face, em);
            // As many lines as the box holds (one at least: a one-line box is the size's 1.25 em, under a face's
            // line height).
            const int lines = line > 0.0f ? std::max(1, static_cast<int>(std::floor(box.h / line + 0.01f))) : 1;
            float top = box.y;
            for (const std::string &text : wrapText(sink, face, em, item.text, box.w, lines)) {
                const float width = sink.textWidth(face, em, text);
                float x = box.x;
                if (item.align == Align::Center)
                    x = box.w > 0.0f ? box.x + (box.w - width) * 0.5f : box.x - width * 0.5f;
                else if (item.align == Align::Right)
                    x = box.w > 0.0f ? box.x + box.w - width : box.x - width;
                if (!text.empty()) sink.text(x, top, em, face, text, color);
                top += line;
            }
            break;
        }
        case ItemKind::ClipPush: {
            const PageRect rect = toPixels(item);
            clips.push_back(clips.empty() ? rect : intersect(clips.back(), rect));
            sink.clip(&clips.back());
            break;
        }
        case ItemKind::ClipPop:
            if (clips.empty()) {
                note("a ClipPop with no ClipPush: ignored");
                break;
            }
            clips.pop_back();
            sink.clip(clips.empty() ? nullptr : &clips.back());
            break;
        default:
            note("item kind " + std::to_string(static_cast<int>(item.kind)) + " unknown: skipped");
            break;
        }
    }
    if (!clips.empty()) sink.clip(nullptr);   // a list that left a clip pushed does not clip what the overlay draws next
}

}  // namespace gt4::ui
