#version 460
#extension GL_GOOGLE_include_directive : require
// B7's parity vertex stage (docs/modern/PLAN.md B7): one item of an instance drawn from the mesh cache instead of from
// gsn_models.comp's per-frame vertices, with the same values to the bit. The vertex is the batch's words as sent
// (scene::MeshRawVertex); the item's job (its constants, lighting rows and tables) is read from the frame's Jobs and
// Words, which gsn_models.comp reads too. It is decoded, lit (gt4_lighting.glsl), transformed (gt4_transform.glsl) and
// handed to gsn.frag (GSN_PARITY) as gsn.vert hands the GS path's vertices. What gsn_models.comp decides per triangle, at
// its last vertex (whether it is drawn: the cull, the hidden test, the fog word's ADC bit; and the vertex's fog word,
// raised for a triangle not drawn), is decided here the same way, for the triangle ending at this vertex in its strip:
// the raw vertex carries the two vertices before it. The ADC bit and a flat triangle's colour are the last vertex's:
// the provoking vertex is the strip's last (the arena's third index, render/modern/parity_view.h), and gsn.frag reads
// them flat. The triangles come in the GS path's vertex order, so Z interpolates as there.
//
// GSN_SIDE (B8 step 2): the side-channel pass's variant, the same stage (so the same Z to the bit for its EQUAL test)
// with the item's view-space position and material for modern_side.frag.
#define GSN_PARITY 1
#include "gsn_common.glsl"

struct Job {
    uint first, data, count, nloop, renderer, chain, dotModel, flags, fogRegister;
    float originX, originY, u, v;
    uint prim, pad0, pad1;
    vec4 matrix[4];
    vec4 scale, offset, clampLimit, guard;
    vec4 previousMatrix[4];
    vec4 previousScale, previousOffset;
};
layout(std430, set = 0, binding = 1) readonly buffer Jobs { Job jobs[]; };
layout(std430, set = 0, binding = 2) readonly buffer Words { uint words[]; };

layout(location = 0) in uvec3 inPositionWords;   // qword k
layout(location = 1) in uvec3 inStWords;         // qword 64 + k
layout(location = 2) in uvec4 inColourWords;     // qword 128 + k
layout(location = 3) in uvec2 inNextWords;       // qword 128 + k + 1 (x, y), for the N.L model's next normal
layout(location = 4) in uint inFlags;            // scene::kRaw*
layout(location = 5) in uvec3 inBefore1;         // qword k - 1 (the strip's vertex before this one)
layout(location = 6) in uvec3 inBefore2;         // qword k - 2

uint base;   // the job's first word
uint word(uint q, uint lane) { return words[base + (q & 255u) * 4u + lane]; }
uvec4 wordsAt(uint q) { return uvec4(word(q, 0u), word(q, 1u), word(q, 2u), word(q, 3u)); }
#include "gt4_lighting.glsl"
#include "gt4_transform.glsl"

const uint kRawLit = 1u, kRawDecoded = 2u, kRawNextDecoded = 4u, kRawStart = 8u, kRawOdd = 16u, kRawBefore1Decoded = 32u,
           kRawBefore2Decoded = 64u;

layout(location = 0) noperspective out vec4 outColour;
layout(location = 1) noperspective out vec3 outStq;
layout(location = 2) noperspective out vec2 outUv;
layout(location = 3) noperspective out float outFog;
layout(location = 4) flat out vec4 outBounds;
layout(location = 5) out vec4 outColourW;
layout(location = 6) out vec3 outStqW;
layout(location = 7) out float outFogW;
layout(location = 8) out float outW;
layout(location = 9) flat out uint outHomogeneous;   // 1, | 2 flat shading, | 4 the ADC bit (gsn.frag GSN_PARITY)
layout(location = 10) flat out vec4 outFlatColour;
#if defined(GSN_SIDE)
// the item's instance (its objectToView, CONTRACTS a.8) and its material (B6), one entry per item drawn (gsn_scene.cpp)
struct SideItem {
    mat4 objectToView;
    uint material, pad0, pad1, pad2;
};
layout(std430, set = 0, binding = 3) readonly buffer SideItems { SideItem sideItems[]; };
layout(location = 11) out vec3 outViewPosition;
layout(location = 12) flat out uint outMaterial;
#endif

invariant gl_Position;   // as gsn.vert's: the same expressions on the same inputs, so the same position (and Z)

vec4 floats(uvec4 w) { return vec4(vuFloat(w.x), vuFloat(w.y), vuFloat(w.z), vuFloat(w.w)); }
// A position as gsn_models.comp has it: decoded (routines 0..2: integers) below NLOOP, else the words as floats.
vec4 positionOf(uvec3 words, bool decoded, uint unpack) {
    if (decoded && unpack <= 2u) return vec4(itof0(words.x), itof0(words.y), itof0(words.z), 0.0);
    return floats(uvec4(words, 0u));
}

void main() {
    const Job job = jobs[draw.parityJob];
    base = job.data;
    // The working copy as gsn_models.comp loads it (sPosition, sSt, sColour, sNormal from qwords k, 64 + k, 128 + k).
    // Lanes the mesh does not keep (positions' and ST's w) are read by nothing below.
    LitVertex v = LitVertex(floats(uvec4(inPositionWords, 0u)), floats(uvec4(inStWords, 0u)), inColourWords,
                            floats(inColourWords));
    vec4 next = floats(uvec4(inNextWords, 0u, 0u));
    const uint unpack = (job.chain >> 4) & 15u, body = job.chain >> 16;
    // gsn_models.comp decodeVertex (the key-frame routines' items are not drawn here: their mesh is per frame)
    if ((inFlags & kRawDecoded) != 0u) {
        if (unpack == 0u || unpack == 1u || unpack == 2u)
            v.position.xyz = vec3(itof0(inPositionWords.x), itof0(inPositionWords.y), itof0(inPositionWords.z));
        if (unpack == 0u) {
            v.st = byteNormal(uvec4(inStWords, 0u), v.st.w);
        } else if (unpack == 1u) {
            v.st.xy = vec2(itof12(inStWords.x), itof12(inStWords.y));
            v.normal = byteNormal(inColourWords, v.normal.w);
            v.colour.xyz = floatBitsToUint(v.normal.xyz);
        } else if (unpack == 2u) {
            v.st.xy = vec2(itof12(inStWords.x), itof12(inStWords.y));
        }
    }
    if ((inFlags & kRawNextDecoded) != 0u && unpack == 1u) next = byteNormal(uvec4(inNextWords, 0u, 0u), next.w);
    if ((job.chain & 1u) != 0u && (inFlags & kRawLit) != 0u) lightVertex(job.dotModel, body, v, next);

    // gsn_models.comp's transform, at constants.t between the previous frame's and this one's
    const bool fog = (job.flags & 1u) != 0u, previous = (job.flags & 4u) != 0u;
    mat4 M = mat4(job.matrix[0], job.matrix[1], job.matrix[2], job.matrix[3]);
    vec4 offset = job.offset, scale = job.scale;
    if (previous && draw.parityT < 1.0)
        gt4Interpolate(M, offset, scale, mat4(job.previousMatrix[0], job.previousMatrix[1], job.previousMatrix[2], job.previousMatrix[3]),
                       job.previousOffset, job.previousScale, draw.parityT);
    const Transformed tr = gt4Transform(M, offset, scale, job.clampLimit, fog, v.position);

    // the fog word and the ADC bit of the triangle ending here (gt4_transform.glsl gt4FogWord), from the clip-space points
    // of the strip's two vertices before it, transformed as this one; the fog lane precise: the compute stage keeps it in
    // shared memory between the multiply and the add, so it fuses nothing
    precise float fogLane = tr.projected.w * scale.w;
    const bool start = (inFlags & kRawStart) != 0u;
    vec4 c1 = vec4(0.0), c2 = vec4(0.0);
    if (!start) {
        c1 = gt4Transform(M, offset, scale, job.clampLimit, fog, positionOf(inBefore1, (inFlags & kRawBefore1Decoded) != 0u, unpack)).clip;
        c2 = gt4Transform(M, offset, scale, job.clampLimit, fog, positionOf(inBefore2, (inFlags & kRawBefore2Decoded) != 0u, unpack)).clip;
    }
    const uint fogged = gt4FogWord(job.renderer, start, (inFlags & kRawOdd) != 0u, c2, c1, tr.clip, offset.w, fogLane);
    const uint fogByte = fogged & 0xFFu;
    const bool adc = (fogged & 0x100u) != 0u;

    // gsn_models.comp writeVertex, then gsn.vert
    const vec3 point = gt4WindowPoint(tr.h, job.originX, job.originY);
    const uvec4 c = v.colour;
    const uint rgba = (c.x & 0xFFu) | (c.y & 0xFFu) << 8 | (c.z & 0xFFu) << 16 | (c.w & 0xFFu) << 24;
    const vec4 colour = unpackUnorm4x8(rgba);
    const vec3 stq = vec3(v.st.x * tr.q, v.st.y * tr.q, tr.q);
    const uint fogValue = fog ? fogByte : job.fogRegister;
    gl_Position = gsnPosition(point.xy, floatBitsToUint(tr.h.z), point.z);
    outColour = colour;
    outStq = stq;
    outUv = vec2(job.u, job.v);
    outFog = float(fogValue);
    outBounds = vec4(1.0, 1.0, 0.0, 0.0);   // (no sprite rectangle: min > max)
    const float w = gl_Position.w;
    outColourW = colour * w;
    outStqW = stq * w;
    outFogW = float(fogValue) * w;
    outW = w;
    outHomogeneous = 1u | ((job.flags & 2u) == 0u ? 2u : 0u) | (adc ? 4u : 0u);
    outFlatColour = colour;
#if defined(GSN_SIDE)
    const SideItem item = sideItems[draw.paritySide];
    outViewPosition = (item.objectToView * vec4(v.position.xyz, 1.0)).xyz;
    outMaterial = item.material;
#endif
}
