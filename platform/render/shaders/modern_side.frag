#version 460
// B8 step 2's side channels (PLAN B8): an item redrawn by modern_parity.vert's GSN_SIDE variant with the depth test
// EQUAL against GT4's Z as its partition left it, writing what the scene stage lights with: the view-space normal (in
// B8's first form the face's, from the view-space position's screen derivatives), the material (B6's MaterialId) and
// the view-space depth. A triangle GT4 does not draw (its ADC bit) writes nothing, as in gsn.frag's parity variant.
layout(location = 9) flat in uint inHomogeneous;
layout(location = 11) in vec3 inViewPosition;
layout(location = 12) flat in uint inMaterial;

layout(location = 0) out vec4 outNormal;     // xyz: view-space unit normal facing the eye; w: 0 (the veiled mark's)
layout(location = 1) out uint outMaterial;
layout(location = 2) out float outDepth;     // view-space depth (positive, away from the eye)

void main() {
    if ((inHomogeneous & 4u) != 0u) discard;
    vec3 n = normalize(cross(dFdx(inViewPosition), dFdy(inViewPosition)));
    if (dot(n, inViewPosition) > 0.0) n = -n;   // (towards the eye at the origin)
    outNormal = vec4(n, 0.0);
    outMaterial = inMaterial;
    outDepth = abs(inViewPosition.z);
}
