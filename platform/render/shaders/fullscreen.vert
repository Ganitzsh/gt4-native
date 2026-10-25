#version 460
// One triangle covering the viewport; uv is (0,0) at the top-left corner, as in Vulkan framebuffer space.
layout(location = 0) out vec2 outUv;
void main() {
    outUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUv * 2.0 - 1.0, 0.0, 1.0);
}
