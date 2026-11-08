#include "draw_list.h"

#include <cmath>
#include <cstdio>

namespace gt4::ui {

std::string describe(const DrawList& list) {
    static const char* const kKinds[] = {"quad", "nine", "grad", "text", "clip", "unclip"};
    static const char* const kFonts[] = {"title", "body", "accent"};
    static const char* const kAligns[] = {"left", "center", "right"};
    std::string out;
    char line[256];
    std::snprintf(line, sizeof line, "canvas %.1f flags %u items %zu\n", static_cast<double>(list.canvasWidth),
                  list.flags, list.items.size());
    out += line;
    for (const Item& item : list.items) {
        // Opacity in 256ths, positions to a tenth: the layout lands them on 1/256 and half units, so the text is the
        // same on every compiler.
        std::snprintf(line, sizeof line, "%s %.1f %.1f %.1f %.1f asset %08x color %08x op %ld/256",
                      kKinds[static_cast<int>(item.kind)], static_cast<double>(item.x), static_cast<double>(item.y),
                      static_cast<double>(item.w), static_cast<double>(item.h), item.asset, item.color,
                      std::lround(item.opacity * 256.0f));
        out += line;
        if (item.kind == ItemKind::Gradient) {
            std::snprintf(line, sizeof line, " to %08x", item.color2);
            out += line;
        }
        if (item.kind == ItemKind::Text) {
            std::snprintf(line, sizeof line, " %s %.0f%s w%u %s \"", kFonts[static_cast<int>(item.font)],
                          static_cast<double>(item.size), item.italic ? " italic" : "", item.weight,
                          kAligns[static_cast<int>(item.align)]);
            out += line;
            out += item.text;
            out += '"';
        }
        out += '\n';
    }
    return out;
}

}  // namespace gt4::ui
