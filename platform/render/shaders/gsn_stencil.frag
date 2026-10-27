#version 460
// The destination alpha test's copy: stencil 1 where the target's alpha has bit 7 set (alpha / 128 >= 1),
// written with stencil REPLACE where this pass does not discard. Same resolution as the target.
layout(set = 0, binding = 0) uniform sampler2D target;

void main() {
    if (texelFetch(target, ivec2(gl_FragCoord.xy), 0).a < 1.0) discard;
}
