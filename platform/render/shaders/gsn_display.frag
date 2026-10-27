#version 460
// The native renderer's displayed buffer as the scene: the part of the target the display circuit reads
// (DISPFB / DISPLAY), stretched over the render area. Targets hold the GS's display-encoded colour / 255; as
// for the GS back end's frame (an sRGB texture), it is decoded to linear for the pipeline, so the
// "original"-like settings give the 8-bit values back. No motion vectors yet.
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(push_constant) uniform Display {
    vec2 offset;   // the displayed rectangle in the target, normalised
    vec2 size;
} display;
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

vec3 linearFromSrgb(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

void main() {
    const vec3 c = clamp(textureLod(frame, display.offset + inUv * display.size, 0.0).rgb, 0.0, 1.0);
    outColor = vec4(linearFromSrgb(c), 1.0);
    outVelocity = vec2(0.0);
}
