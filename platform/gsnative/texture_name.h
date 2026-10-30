#pragma once
// A texture's name in a PCSX2-format texture pack (docs/TEXTURE_PACKS.md): hashes of the texture's bytes in
// GS memory and of its palette, and the TEX0 / TEXA bits that change how those bytes read, computed the way
// PCSX2 computes them when it dumps and loads replacements, so that packs made with it apply here.
#include "gs_memory.h"
#include "texture_pack.h"

namespace gt4::gsn {

// A mipmap level beyond the first: where it is in memory.
struct MipAddress {
    uint32_t tbp = 0, tbw = 0;
};

// The pack's name for the texture TEX0 describes, as drawn with `sampler` (its CLAMP region, its mipmaps:
// `mips` holds levels 1..n of a mipmapped draw), with TEXA for 16 and 24-bit formats and the CLUT buffer as
// loaded for the draw. PCSX2 names one texture differently by how it is drawn (a region, a recent partial
// upload, the mip chain): the names the pack can have for it are tried (its regions for these TEX0 / TEXA
// bits, with and without the chain). False when the pack has none of them.
bool findTextureName(const GsMemory& mem, const GsMemory::Tex0& tex0, const Texa& texa, const gfx::Sampler& sampler,
                     const MipAddress* mips, uint32_t mipCount, const TexturePack& pack, TextureName& name);

}  // namespace gt4::gsn
