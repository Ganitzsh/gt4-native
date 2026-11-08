#version 460
// B8 step 3's debug view (GT4_SIDE_VIEW=normal|material|depth|veiled): a side channel shown in place of the scene target's
// colour at its Light, to check what the scene stage will light with. The target holds colour / 255 (gsn.frag), so
// 1.0 is full brightness; alpha is left as GT4 wrote it (the write mask keeps it).
layout(set = 0, binding = 0) uniform sampler2D normalChannel;
layout(set = 0, binding = 1) uniform usampler2D materialChannel;
layout(set = 0, binding = 2) uniform sampler2D depthChannel;
layout(push_constant) uniform View { uint channel; float depthRange; } view;   // channel: 0 normal, 1 material, 2 depth, 3 veiled
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColour;

vec3 hue(uint id) {   // a stable colour per material id (0: black, GT4's colour as it is)
    if (id == 0u) return vec3(0.0);
    uint h = id * 2654435761u;
    return vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) / 255.0 * 0.8 + 0.2;
}

void main() {
    const ivec2 at = ivec2(gl_FragCoord.xy);
    vec3 c;
    if (view.channel == 0u) {
        const vec4 n = texelFetch(normalChannel, at, 0);
        c = dot(n.xyz, n.xyz) > 0.0 ? n.xyz * 0.5 + 0.5 : vec3(0.0);
    } else if (view.channel == 1u) {
        c = hue(texelFetch(materialChannel, at, 0).r);
    } else if (view.channel == 3u) {
        c = vec3(texelFetch(normalChannel, at, 0).w > 0.0 ? 1.0 : 0.0);
    } else {
        const float d = texelFetch(depthChannel, at, 0).r;
        c = d > 0.0 ? vec3(1.0 - clamp(d / view.depthRange, 0.0, 1.0)) : vec3(0.0);
    }
    outColour = vec4(c, 0.0);
}
