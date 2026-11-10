#version 460
// B8 step 2's veiled marking: a blended, Z-tested draw of the partition that is no view's item (smoke, particles,
// glare: a view's own blended layers are its surface), through gsn.vert, lying strictly in front of the surface (depth
// test GREATER: a coplanar layer is the surface), marks the side channels' pixel veiled: the normal channel's w (its
// write mask lets only that through). How a veiled pixel is lit is B10's.
// The pass's write mask is alpha alone on every attachment (one blend state for all: independentBlend isn't
// required), so the lit channel's alpha is written here too: 0, which nothing reads.
layout(location = 0) out vec4 outNormal;
layout(location = 3) out vec4 outLit;
void main() {
    outNormal = vec4(0.0, 0.0, 0.0, 1.0);
    outLit = vec4(0.0);
}
