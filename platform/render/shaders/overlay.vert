#version 460
// 2D layer (HUD, menus, text): pixel-space quads in the swapchain, already laid out on the CPU by the
// canvas anchoring rules (platform/render/overlay.cpp).
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(push_constant) uniform Push { vec2 targetSize; float srgbTarget; } push;
void main() {
    outUv = inUv;
    outColor = inColor;
    gl_Position = vec4(inPosition / push.targetSize * 2.0 - 1.0, 0.0, 1.0);
}
