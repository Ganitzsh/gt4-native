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
vec2 fieldPoint(sampler2D t, vec2 offset, vec2 size, bool lower, vec2 uv) {
    vec2 p = offset + uv * size;
    if (display.clean > 0.5 && lower)
        p.y = min(p.y, offset.y + size.y - display.line - 0.5 / float(textureSize(t, 0).y));
    return p;
}

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

float fieldLuma(sampler2D t, vec2 offset, vec2 size, bool lower, vec2 uv) {
    return dot(clamp(textureLod(t, fieldPoint(t, offset, size, lower, uv), 0.0).rgb, 0.0, 1.0), kLuma);
}

// How much the two fields differ around this pixel (display.field_mode = clean). Each field is drawn on its own, half a
// line from the other, so a still stroke one line thick is whole on one field's line and split over two of the
// other's: the fields' lumas are compared smoothed over a step above and below (1-2-1), where a still picture differs
// by about 0.1 and a moved one by several times that. The largest difference over three rows and three columns, a step
// apart, is taken, so a letter that moved is shown from one field as a whole rather than in patches. The steps are
// fractions of the displayed rectangle, so they span the same GS lines and pixels at any output size: 2/720 of its
// height and 3/1280 of its width, as tuned (the rectangle drawn at 1280x720, menus_boot.cap's track menus, against each
// field's own motion two fields later).
const vec2 kFieldStep = vec2(3.0 / 1280.0, 2.0 / 720.0);

float fieldsDiffer(bool currentLower, bool otherLower) {
    float moved = 0.0;
    for (int i = -1; i <= 1; ++i) {
        float c[5], o[5];
        for (int j = 0; j < 5; ++j) {
            const vec2 uv = inUv + vec2(float(i), float(j - 2)) * kFieldStep;
            c[j] = fieldLuma(frame, display.offset, display.size, currentLower, uv);
            o[j] = fieldLuma(otherField, display.otherOffset, display.otherSize, otherLower, uv);
        }
        for (int j = 1; j <= 3; ++j)
            moved = max(moved, abs((c[j - 1] + 2.0 * c[j] + c[j + 1]) - (o[j - 1] + 2.0 * o[j] + o[j + 1])) * 0.25);
    }
    return moved;
}

void main() {
    const bool blended = display.blend > 0.0;
    const bool currentLower = blended && display.offset.y > display.otherOffset.y;
    const bool otherLower = blended && display.otherOffset.y > display.offset.y;
    // display.field_mode = clean: the displayed rectangle's last GS line is the one a TV hides below the picture
    // (overscan), where GT4's menus park their next text line, drawn in the lower field. Shown black, as PCSX2's
    // software picture shows it: the motion test below would otherwise show that field alone there, parked text and
    // all (docs/modern/spikes/E4-fields-vs-pcsx2.md).
    if (display.clean > 0.5 && blended && inUv.y > 1.0 - display.line / display.size.y) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        outVelocity = vec2(0.0);
        return;
    }
    vec3 c = clamp(textureLod(frame, fieldPoint(frame, display.offset, display.size, currentLower, inUv), 0.0).rgb, 0.0, 1.0);
    if (blended) {
        const vec2 q = fieldPoint(otherField, display.otherOffset, display.otherSize, otherLower, inUv);
        const vec3 o = clamp(textureLod(otherField, q, 0.0).rgb, 0.0, 1.0);
        float w = display.blend;
        if (display.clean > 0.5) {
            // Where something moved between the two fields (a menu line sliding in), the blend would show it twice:
            // there this field alone.
            const float moved = fieldsDiffer(currentLower, otherLower);
            w *= 1.0 - smoothstep(0.10, 0.20, moved);
        }
        c = mix(c, o, w);
    }
    outColor = vec4(linearFromSrgb(c), 1.0);
    outVelocity = vec2(0.0);
}
