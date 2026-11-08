#version 460
// B8 step 2: the side-channel pass's reset, one triangle covering the target at the depth the partition's Z clear left
// (gsn.vert's sprite depth, computed on the CPU the same way). With the depth test NOT_EQUAL it reaches exactly the
// pixels whose Z this partition wrote; gsn_side_reset.frag clears their channels (material 0: GT4's colour as it is).
layout(push_constant) uniform Reset { float depth; } reset;
void main() {
    const vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, reset.depth, 1.0);
}
