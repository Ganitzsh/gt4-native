#ifndef GT4_CUBE_GLSL
#define GT4_CUBE_GLSL
// Direction through texel (x, y) of a cube face, Vulkan/D3D face order +X -X +Y -Y +Z -Z.
vec3 cubeDirection(uint face, vec2 texel, float size) {
    vec2 uv = (texel + 0.5) / size * 2.0 - 1.0;
    vec3 d;
    if (face == 0u) d = vec3(1.0, -uv.y, -uv.x);
    else if (face == 1u) d = vec3(-1.0, -uv.y, uv.x);
    else if (face == 2u) d = vec3(uv.x, 1.0, uv.y);
    else if (face == 3u) d = vec3(uv.x, -1.0, -uv.y);
    else if (face == 4u) d = vec3(uv.x, -uv.y, 1.0);
    else d = vec3(-uv.x, -uv.y, -1.0);
    return normalize(d);
}
#endif
