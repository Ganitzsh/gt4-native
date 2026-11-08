// GT4's 3D renderers' transform of one vertex (platform/gs/renderers3d.cpp; docs/VU1_PROGRAMS.md), shared by
// gsn_models.comp (the GS path) and modern_parity.vert (B7): the clip-space point, its 1 / w, the clamped projected point
// and the homogeneous window coordinates with GT4's Z (x 16 without fog). The same operations in both, so a vertex gets
// the same Z on both paths and GT4's depth ties stay ties (RISKS R3). Needs gt4_lighting.glsl (sat, divide) first.
// All 'precise' (no contraction into fused multiply-adds): the compiler of each stage would otherwise fuse its own way.

struct Transformed {
    vec4 clip;        // M p, saturated
    float q;          // 1 / clip.w, as the VU divides
    vec4 projected;   // min(clip, clampLimit) / w, saturated
    vec4 h;           // homogeneous window coordinates: x, y, Z, w
};

// The transform at `t` between the previous frame's (P, previousOffset, previousScale) and this one's (display
// interpolation, gsn_models.comp's constants.t): each value blended as previous x (1 - t) + current x t. Shared and
// precise: the compute and the vertex stage blend to the same bits, so the blend can't move a near-equal Z.
void gt4Interpolate(inout mat4 M, inout vec4 offset, inout vec4 scale, mat4 P, vec4 previousOffset, vec4 previousScale, float t) {
    const float rest = 1.0 - t;
    for (int k = 0; k < 4; ++k) {
        precise vec4 m = P[k] * rest + M[k] * t;
        M[k] = m;
    }
    precise vec4 o = previousOffset * rest + offset * t, s = previousScale * rest + scale * t;
    offset = o;
    scale = s;
}

Transformed gt4Transform(mat4 M, vec4 offset, vec4 scale, vec4 clampLimit, bool fog, vec4 p) {
    Transformed r;
    precise vec4 clip = M[0] * p.x + M[1] * p.y + M[2] * p.z + M[3];
    r.clip = sat(clip);
    r.q = divide(1.0, r.clip.w);
    precise vec4 projected = min(r.clip, clampLimit) * r.q;
    r.projected = sat(projected);
    const float w = r.clip.w;
    precise vec4 h = vec4(offset.x * w + scale.x * min(r.clip.x, clampLimit.x), offset.y * w + scale.y * min(r.clip.y, clampLimit.y),
                          (offset.z * w + scale.z * min(r.clip.z, clampLimit.z)) * (fog ? 1.0 : 16.0), w);
    r.h = h;
    return r;
}

// The renderers' per-triangle decision, made at a triangle's last vertex (renderers3d.cpp; gsn_models.comp): clip-space
// points of the strip's vertices k - 2, k - 1 and k. A triangle wholly outside one clip plane is hidden (renderers 3..5).
uint clipFlags(vec4 p) {
    const float limit = abs(p.w);
    uint f = 0u;
    for (int i = 0; i < 3; ++i) {
        if (p[i] > limit) f |= 1u << (i * 2);
        if (p[i] < -limit) f |= 2u << (i * 2);
    }
    return f;
}
// The facing: the homogeneous determinant of the three points, negated for a strip's odd triangles.
float homogeneousFacing(vec4 a, vec4 b, vec4 c, bool oddOrder) {
    precise float d = a.x * (b.y * c.w - c.y * b.w) - a.y * (b.x * c.w - c.x * b.w) + a.w * (b.x * c.y - c.x * b.y);
    return oddOrder ? -d : d;
}
// The vertex's fog word (gsn::Vertex::fog's byte in bits 0..7) and ADC (bit 8: the triangle ending at it is not drawn):
// its fog lane over the offset, raised past the byte for a triangle not drawn (the strip's first two vertices, a culled or
// hidden one). The raise is added before the fog lane, so its rounding can move the byte the vertex gives its other
// triangles: kept, as the VU does it.
uint gt4FogWord(uint renderer, bool start, bool odd, vec4 c2, vec4 c1, vec4 c0, float offsetW, float fogLane) {
    float raise = 0.0, after = 0.0;
    if (renderer == 0u) {
        raise = start ? 2048.0 : 0.0;
    } else if (renderer <= 2u) {
        bool culled = start;
        if (!culled) {
            const float z = homogeneousFacing(c2, c1, c0, odd);
            culled = renderer == 2u ? !negativeFlag(z) : !positiveFlag(z);
        }
        raise = culled ? 2048.0 : 0.0;
    } else {
        bool hidden = start;
        if (!hidden && (clipFlags(c2) & clipFlags(c1) & clipFlags(c0)) != 0u) hidden = true;
        raise = hidden ? (renderer == 3u ? 2048.0 : 3072.0) : 0.0;
        if (renderer != 3u && !start) {
            if (!positiveFlag(homogeneousFacing(c2, c1, c0, odd != (renderer == 5u)))) after = 3072.0;
        }
    }
    precise float fogWord = (offsetW + raise) + fogLane;
    fogWord = fogWord + after;
    const uint fixedWord = fixedPoint(fogWord, 16.0);
    return ((fixedWord >> 4) & 0xFFu) | ((fixedWord >> 15) & 1u) << 8;
}

// The vertex as the GS path's vertex buffer holds it (gsn_models.comp writeVertex): x and y less the job's origin
// times w (w 0 made the smallest normal: 0 means "not homogeneous" to the vertex stage).
vec3 gt4WindowPoint(vec4 h, float originX, float originY) {
    float w = h.w;
    if (w == 0.0) w = kMinNormal;
    precise vec3 point = vec3(h.x - originX * w, h.y - originY * w, w);
    return point;
}
