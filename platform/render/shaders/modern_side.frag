#version 460
// B8 step 2's side channels (PLAN B8): an item redrawn by modern_parity.vert's GSN_SIDE variant with the depth test
// EQUAL against GT4's Z as its partition left it, writing what the scene stage lights with: the view-space normal (in
// B8's first form the face's, from the view-space position's screen derivatives), the material (B6's MaterialId) and
// the view-space depth. A triangle GT4 does not draw (its ADC bit) writes nothing, as in gsn.frag's parity variant.
// B10's channel (PLAN B10, "Channels"): GT4's lit vertex colour as the GS interpolates it, before texture and fog,
// taken as gsn.frag's parity variant takes it (flat: the provoking vertex's; else perspective-correct). The GS
// modulates the texture by it per pixel (0x80 is 1.0), so GT4's pixel over this channel is the texture, to rounding:
// the surface's own colour, which the scene stage lights.
layout(location = 5) in vec4 inColourW;
layout(location = 8) in float inW;
layout(location = 9) flat in uint inHomogeneous;
layout(location = 10) flat in vec4 inFlatColour;
layout(location = 11) in vec3 inViewPosition;
layout(location = 12) flat in uint inMaterial;

layout(location = 0) out vec4 outNormal;     // xyz: view-space unit normal facing the eye; w: 0 (the veiled mark's)
layout(location = 1) out uint outMaterial;
layout(location = 2) out float outDepth;     // view-space depth (positive, away from the eye)
layout(location = 3) out vec4 outLit;        // rgb: the lit vertex colour, 1.0 = 0xFF (so 128 / 255 modulates by 1);
                                             // a: the vertex alpha the same way (the veiled marking may overwrite it)

void main() {
    if ((inHomogeneous & 4u) != 0u) discard;
    vec3 n = normalize(cross(dFdx(inViewPosition), dFdy(inViewPosition)));
    if (dot(n, inViewPosition) > 0.0) n = -n;   // (towards the eye at the origin)
    outNormal = vec4(n, 0.0);
    outMaterial = inMaterial;
    outDepth = abs(inViewPosition.z);
    outLit = (inHomogeneous & 2u) != 0u ? inFlatColour : inColourW / inW;
}
