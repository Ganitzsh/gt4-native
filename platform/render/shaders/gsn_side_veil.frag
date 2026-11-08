#version 460
// B8 step 2's veiled marking: a blended, Z-tested draw of the partition that is no view's item (smoke, particles,
// glare: a view's own blended layers are its surface), through gsn.vert, lying strictly in front of the surface (depth
// test GREATER: a coplanar layer is the surface), marks the side channels' pixel veiled: the normal channel's w (its
// write mask lets only that through). How a veiled pixel is lit is B10's.
layout(location = 0) out vec4 outNormal;
void main() {
    outNormal = vec4(0.0, 0.0, 0.0, 1.0);
}
