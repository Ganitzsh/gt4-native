// stb_image_write (third_party/stb, unmodified): the PNG writer of the dump key's frames (renderer.cpp, PLAN.md C6b).
// Built here, in its own file, without the project's warning flags (cmake/Host.cmake), as stb_truetype is.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8   // (paths in UTF-8 on Windows, as the host's other files)
#include "stb/stb_image_write.h"
