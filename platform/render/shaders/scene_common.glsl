// Resources of the test scene (platform/render/test_scene.cpp). The R1 game renderer replaces this scene;
// what it must hand to the post pipeline is described in docs/RENDERING.md ("Scene contract").
#ifndef GT4_SCENE_COMMON_GLSL
#define GT4_SCENE_COMMON_GLSL

struct Instance {
    mat4 model;
    mat4 previousModel;
    vec4 color;      // base colour (linear), a = clear-coat amount
    vec4 material;   // x roughness, y metallic, z emissive strength, w texture layer (< 0: none)
    vec4 uv;         // xy texture repeat, z heat (heat-haze source), w unused
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 viewProjection;          // jittered
    mat4 viewProjectionUnjittered;
    mat4 previousViewProjection;  // unjittered, previous rendered frame
    mat4 inverseViewProjection;   // unjittered
    mat4 lightViewProjection;
    vec4 cameraPosition;          // xyz, w = time in seconds
    vec4 sunDirection;            // xyz towards the sun, w = sun illuminance scale
    vec4 sunColor;                // rgb, w = sky/ambient scale
    vec4 fogParams;               // x density, y height falloff, z base height, w mode (0 linear, 1 height, 2 height + sun)
    vec4 fogColor;                // rgb, w = linear fog end distance
    vec4 shadowParams;            // x mode (0 blob, 1 2x2, 2 3x3, 3 poisson, 4 pcss), y texel size, z bias, w light size
    vec4 envParams;               // x mip count, y prefiltered (0/1), z intensity, w unused
    vec4 renderInfo;              // xy render size, z frame index, w action flag
    vec4 jitter;                  // xy sub-pixel jitter in pixels (content shift), zw unused
    vec4 blobs[8];                // blob shadows: xz centre, z radius, w strength
} frame;

layout(set = 0, binding = 1, std430) readonly buffer Instances { Instance instances[]; };

#endif
