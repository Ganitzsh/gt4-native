// stb_truetype's implementation (third_party/stb, v1.26, public domain or MIT, unmodified), built without our warning
// flags. It only ever reads the bundled UI fonts (platform/ui/host/sdf_font.cpp), never a user-supplied font: it is not
// hardened against hostile files.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
