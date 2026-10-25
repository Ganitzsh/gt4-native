#include "smaa_textures.h"
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

namespace gt4::smaa {
const unsigned char* areaTexture(size_t& bytes, unsigned& width, unsigned& height) {
    bytes = sizeof(areaTexBytes);
    width = AREATEX_WIDTH;
    height = AREATEX_HEIGHT;
    return areaTexBytes;
}
const unsigned char* searchTexture(size_t& bytes, unsigned& width, unsigned& height) {
    bytes = sizeof(searchTexBytes);
    width = SEARCHTEX_WIDTH;
    height = SEARCHTEX_HEIGHT;
    return searchTexBytes;
}
}
