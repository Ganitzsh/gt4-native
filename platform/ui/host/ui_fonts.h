#pragma once
// The UI fonts embedded in the program (assets/ui/fonts, SIL OFL, byte for byte; cmake/Host.cmake generates the
// table), so a release needs no assets folder beside gt4_host.
#include <cstddef>

namespace gt4::ui {

struct EmbeddedFont {
    const char *name;            // the file's name without .ttf
    const unsigned char *data;
    size_t size;
};

extern const EmbeddedFont kEmbeddedFonts[];
extern const size_t kEmbeddedFontCount;

}  // namespace gt4::ui
