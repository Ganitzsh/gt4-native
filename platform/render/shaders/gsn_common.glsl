// The native renderer's draw constants (docs/NATIVE_RENDERER.md), shared by gsn.vert and gsn.frag.
layout(push_constant) uniform Draw {
    vec2 targetSize;     // the target's size in render pixels (GS pixels times scale)
    vec2 scale;          // render pixels per GS pixel, per axis
    vec2 textureSize;    // the texture's size in texels (2^TW, 2^TH)
    vec2 sourceOffset;   // texture from a target: texel (0, 0) in the target's GS pixels
    vec2 sourceSize;     // ... and the target's size in GS pixels
    uint flags;          // see below
    uint tex;            // TFX (bits 0-1), TCC (2), TEXA: TA0 (8-15), TA1 (16-23), AEM (24)
    uint fogColour;      // FOGCOL, R in the low byte
    uint alphaTest;      // ATE (0), ATST (1-3), AREF (4-11), AFAIL (12-13)
    uint blend;          // A (0-1), B (2-3), C (4-5), D (6-7), FIX (8-15): ALPHA
    uint clampU, clampV; // WMS/WMT (0-1), MINU/MINV (2-11), MAXU/MAXV (12-21): CLAMP
    uint lod;            // LCM (0), L (1-2), K (3-14, signed 7.4), MXL (16-18): TEX1
} draw;

bool atNativeScale() { return draw.scale.x <= 1.0 && draw.scale.y <= 1.0; }

// flags
const uint kTextured = 1u;        // TME
const uint kUv = 2u;              // FST: UV coordinates in texels
const uint kFog = 4u;             // FGE
const uint kBlend = 8u;           // ABE (blending in the shader's dual-source output)
const uint kPabe = 16u;           // per-pixel alpha blending: only where As >= 0x80
const uint kFba = 32u;            // FBA: alpha |= 0x80 when written
const uint kFromTarget = 64u;     // the texture is a render target (colour / 255, alpha / 128)
const uint kTarget24 = 128u;      // ... in a 24-bit format: alpha from TEXA
const uint kDestAlphaBlend = 256u;// C = Ad: the pipeline's fixed-function factors do the blend
const uint kNearest = 512u;       // point sampling: the GS's texel, its wrap modes, by texel fetch
const uint kAlphaInvert = 2048u;  // the alpha test's failing pixels (AFAIL's pass)
const uint kSnap = 1024u;
const uint kSnapFiltered = 16384u;// a reinterpreted texture drawn scaled: GS bilinear over GS texels
const uint kMipmapped = 4096u;    // the GS's mipmap levels: the sampler's trilinear filtering
const uint kReplaced = 32768u;    // a texture pack's image: any size, sampled normalised with its own mips
const uint kGeneratedMips = 8192u;// ... levels made from level 0 (the GS has none): alpha from level 0         // coordinates at the GS pixel's sample point, not the render pixel's
