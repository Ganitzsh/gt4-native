#version 460
// The native renderer's displayed buffer as the scene: the part of the target the display circuit reads
// (DISPFB / DISPLAY), stretched over the render area. Targets hold the GS's display-encoded colour / 255; as
// for the GS back end's frame (an sRGB texture), it is decoded to linear for the pipeline, so the
// "original"-like settings give the 8-bit values back. No motion vectors yet. In field mode the field shown
// before is mixed in (`blend`), so the two fields of an interlaced screen do not bob (gsn_scene.cpp).
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(set = 0, binding = 1) uniform sampler2D otherField;
layout(push_constant) uniform Display {
    vec2 offset;   // the displayed rectangle in the target, normalised
    vec2 size;
    vec2 otherOffset;   // ... and in the other field's target
    vec2 otherSize;
    float blend;   // how much of the other field
} display;
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

// A point of the displayed rectangle, kept half a texel inside it: the bilinear filter would otherwise take in
// the row or column just past it, which the GS never shows (GT4's menus park their outgoing text there).
vec2 inside(sampler2D t, vec2 offset, vec2 size, vec2 uv) {
    const vec2 halfTexel = 0.5 / vec2(textureSize(t, 0));
    return clamp(offset + uv * size, offset + halfTexel, offset + size - halfTexel);
}

vec3 linearFromSrgb(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

void main() {
    vec3 c = clamp(textureLod(frame, inside(frame, display.offset, display.size, inUv), 0.0).rgb, 0.0, 1.0);
    if (display.blend > 0.0) {
        const vec3 o = clamp(textureLod(otherField, inside(otherField, display.otherOffset, display.otherSize, inUv), 0.0).rgb, 0.0, 1.0);
        c = mix(c, o, display.blend);
    }
    outColor = vec4(linearFromSrgb(c), 1.0);
    outVelocity = vec2(0.0);
}
