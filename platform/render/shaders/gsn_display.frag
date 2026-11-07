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
    float clean;   // display.field_mode = clean (1) or blend (0)
    float line;    // one GS line of the displayed rectangle, normalised
} display;
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

vec3 linearFromSrgb(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

// A point of a field's rectangle; in the clean field mode the lower of the two fields (the one whose rectangle starts
// a line lower) does not show its last line: the line a TV hides below the other field's, where GT4's menus park
// their outgoing text, a faint row at the bottom when it is blended in.
vec2 fieldPoint(sampler2D t, vec2 offset, vec2 size, bool lower) {
    vec2 p = offset + inUv * size;
    if (display.clean > 0.5 && lower)
        p.y = min(p.y, offset.y + size.y - display.line - 0.5 / float(textureSize(t, 0).y));
    return p;
}

void main() {
    const bool blended = display.blend > 0.0;
    const bool currentLower = blended && display.offset.y > display.otherOffset.y;
    const bool otherLower = blended && display.otherOffset.y > display.offset.y;
    vec3 c = clamp(textureLod(frame, fieldPoint(frame, display.offset, display.size, currentLower), 0.0).rgb, 0.0, 1.0);
    if (blended) {
        const vec2 q = fieldPoint(otherField, display.otherOffset, display.otherSize, otherLower);
        const vec3 o = clamp(textureLod(otherField, q, 0.0).rgb, 0.0, 1.0);
        float w = display.blend;
        if (display.clean > 0.5) {
            // Still where this field's colour lies within the other field's at this line and the lines above and
            // below (the fields are half a line apart: a still edge falls between them); elsewhere something moved
            // between the two fields (a menu line sliding in) and the blend would show it twice: this field alone.
            const vec2 dy = vec2(0.0, display.line);
            const vec3 up = clamp(textureLod(otherField, q - dy, 0.0).rgb, 0.0, 1.0);
            const vec3 down = clamp(textureLod(otherField, q + dy, 0.0).rgb, 0.0, 1.0);
            const vec3 lo = min(min(up, down), o), hi = max(max(up, down), o);
            const vec3 outside = max(lo - c, c - hi);
            const float moved = max(max(outside.r, outside.g), outside.b);
            w *= 1.0 - smoothstep(0.03, 0.09, moved);
        }
        c = mix(c, o, w);
    }
    outColor = vec4(linearFromSrgb(c), 1.0);
    outVelocity = vec2(0.0);
}
