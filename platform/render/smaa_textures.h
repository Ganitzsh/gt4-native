#pragma once
// SMAA's precomputed lookup textures (third_party/smaa, Jimenez et al., see THIRD_PARTY_NOTICES.md).
#include <cstddef>
namespace gt4::smaa {
// 160 x 560, two 8-bit channels (R8G8_UNORM), rows top to bottom.
const unsigned char* areaTexture(size_t& bytes, unsigned& width, unsigned& height);
// 66 x 33, one 8-bit channel (R8_UNORM).
const unsigned char* searchTexture(size_t& bytes, unsigned& width, unsigned& height);
}
