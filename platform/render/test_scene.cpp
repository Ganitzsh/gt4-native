#include "test_scene.h"

#include "gt4_shaders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace gt4 {
namespace {

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};

struct Mesh {
    uint32_t firstIndex = 0, indexCount = 0;
    int32_t vertexOffset = 0;
};

enum MeshId : uint32_t { kBox, kSphere, kCylinder, kPlane, kTrack, kMeshCount };

// std430 layout of scene_common.glsl's Instance.
struct InstanceData {
    Mat4 model, previousModel;
    Vec4 color, material, uv;
};

// std140 layout of scene_common.glsl's Frame block.
struct FrameData {
    Mat4 viewProjection, viewProjectionUnjittered, previousViewProjection, inverseViewProjection, lightViewProjection;
    Vec4 cameraPosition, sunDirection, sunColor, fogParams, fogColor, shadowParams, envParams, renderInfo, jitter;
    Vec4 blobs[8];
};

struct Draw {
    MeshId mesh;
    Mat4 model;
    Vec4 color, material, uv;
};

constexpr float kTrackRadius = 120.0f;
constexpr float kTrackWidth = 16.0f;
constexpr float kTickSeconds = 1.0f / 60.0f;
constexpr int kCars = 6;
constexpr uint32_t kTextureSize = 512;
constexpr uint32_t kTextureLayers = 4;

struct MeshBuilder {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Mesh begin() const { return {static_cast<uint32_t>(indices.size()), 0, static_cast<int32_t>(vertices.size())}; }
    void end(Mesh& mesh) const { mesh.indexCount = static_cast<uint32_t>(indices.size()) - mesh.firstIndex; }
    uint32_t vertex(Vec3 p, Vec3 n, float u, float v, int32_t base) {
        vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {u, v}});
        return static_cast<uint32_t>(static_cast<int32_t>(vertices.size()) - 1 - base);
    }
};

void buildBox(MeshBuilder& b, Mesh& mesh) {
    mesh = b.begin();
    const Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& n : normals) {
        const Vec3 u = std::fabs(n.y) > 0.5f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 v = cross(n, u);
        const uint32_t start = static_cast<uint32_t>(b.vertices.size()) - static_cast<uint32_t>(mesh.vertexOffset);
        for (int i = 0; i < 4; ++i) {
            const float su = (i == 1 || i == 2) ? 1.0f : -1.0f, sv = (i >= 2) ? 1.0f : -1.0f;
            b.vertex((n + u * su + v * sv) * 0.5f, n, su * 0.5f + 0.5f, sv * 0.5f + 0.5f, mesh.vertexOffset);
        }
        // Counter-clockwise when seen from outside (corners -u-v, +u-v, +u+v, -u+v with v = n x u).
        for (uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) b.indices.push_back(start + index);
    }
    b.end(mesh);
}

void buildSphere(MeshBuilder& b, Mesh& mesh, int rings, int segments) {
    mesh = b.begin();
    for (int r = 0; r <= rings; ++r) {
        const float phi = kPi * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segments);
            const Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            b.vertex(n * 0.5f, n, static_cast<float>(s) / static_cast<float>(segments),
                     static_cast<float>(r) / static_cast<float>(rings), mesh.vertexOffset);
        }
    }
    const uint32_t row = static_cast<uint32_t>(segments + 1);
    for (uint32_t r = 0; r < static_cast<uint32_t>(rings); ++r)
        for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
            const uint32_t a = r * row + s, c = a + row;
            for (uint32_t index : {a, a + 1, c, a + 1, c + 1, c}) b.indices.push_back(index);
        }
    b.end(mesh);
}

void buildCylinder(MeshBuilder& b, Mesh& mesh, int segments) {
    // Unit cylinder along Y from -0.5 to 0.5, radius 0.5, with caps.
    mesh = b.begin();
    for (int s = 0; s <= segments; ++s) {
        const float theta = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segments);
        const Vec3 n{std::cos(theta), 0, std::sin(theta)};
        b.vertex(Vec3{n.x * 0.5f, -0.5f, n.z * 0.5f}, n, static_cast<float>(s) / static_cast<float>(segments), 0, mesh.vertexOffset);
        b.vertex(Vec3{n.x * 0.5f, 0.5f, n.z * 0.5f}, n, static_cast<float>(s) / static_cast<float>(segments), 1, mesh.vertexOffset);
    }
    for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
        const uint32_t a = s * 2;
        for (uint32_t index : {a, a + 1, a + 2, a + 1, a + 3, a + 2}) b.indices.push_back(index);
    }
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap ? 0.5f : -0.5f;
        const Vec3 n{0, cap ? 1.0f : -1.0f, 0};
        const uint32_t center = b.vertex(Vec3{0, y, 0}, n, 0.5f, 0.5f, mesh.vertexOffset);
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segments);
            b.vertex(Vec3{std::cos(theta) * 0.5f, y, std::sin(theta) * 0.5f}, n, std::cos(theta) * 0.5f + 0.5f,
                     std::sin(theta) * 0.5f + 0.5f, mesh.vertexOffset);
        }
        for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
            if (cap) for (uint32_t index : {center, center + s + 2, center + s + 1}) b.indices.push_back(index);
            else for (uint32_t index : {center, center + s + 1, center + s + 2}) b.indices.push_back(index);
        }
    }
    b.end(mesh);
}

void buildPlane(MeshBuilder& b, Mesh& mesh) {
    // Unit quad in XZ facing +Y; uv spans 0..1 (scaled per instance).
    mesh = b.begin();
    for (int i = 0; i < 4; ++i) {
        const float x = (i == 1 || i == 2) ? 0.5f : -0.5f, z = (i >= 2) ? 0.5f : -0.5f;
        b.vertex(Vec3{x, 0, z}, Vec3{0, 1, 0}, x + 0.5f, z + 0.5f, mesh.vertexOffset);
    }
    for (uint32_t index : {0u, 2u, 1u, 0u, 3u, 2u}) b.indices.push_back(index);
    b.end(mesh);
}

void buildTrack(MeshBuilder& b, Mesh& mesh) {
    // Annulus at y = 0.02: u across the road (0..1), v along it, one texture repeat every 8 m.
    mesh = b.begin();
    const int segments = 720;
    const float inner = kTrackRadius - kTrackWidth * 0.5f, outer = kTrackRadius + kTrackWidth * 0.5f;
    const float circumference = 2.0f * kPi * kTrackRadius;
    for (int s = 0; s <= segments; ++s) {
        const float t = static_cast<float>(s) / static_cast<float>(segments);
        const float theta = 2.0f * kPi * t;
        const float c = std::cos(theta), sn = std::sin(theta);
        const float v = t * circumference / 8.0f;
        b.vertex(Vec3{c * inner, 0.02f, sn * inner}, Vec3{0, 1, 0}, 0.0f, v, mesh.vertexOffset);
        b.vertex(Vec3{c * outer, 0.02f, sn * outer}, Vec3{0, 1, 0}, 1.0f, v, mesh.vertexOffset);
    }
    for (uint32_t s = 0; s < static_cast<uint32_t>(segments); ++s) {
        const uint32_t a = s * 2;
        for (uint32_t index : {a, a + 2, a + 1, a + 1, a + 2, a + 3}) b.indices.push_back(index);
    }
    b.end(mesh);
}

// --- Procedural textures (sRGB-encoded RGBA8) ---------------------------------------------------------

float hashNoise(int x, int y, int seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

float valueNoise(float x, float y, int seed, int period) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi), fy = y - static_cast<float>(yi);
    auto at = [&](int ax, int ay) { return hashNoise(((ax % period) + period) % period, ((ay % period) + period) % period, seed); };
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = at(xi, yi) + (at(xi + 1, yi) - at(xi, yi)) * sx;
    const float b = at(xi, yi + 1) + (at(xi + 1, yi + 1) - at(xi, yi + 1)) * sx;
    return a + (b - a) * sy;
}

float toSrgb(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}
float fromSrgb(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

struct Rgb { float r, g, b; };

Rgb texel(uint32_t layer, float u, float v) {
    // u, v in [0, 1); every pattern tiles.
    switch (layer) {
    case 0: {  // asphalt with lane markings (u across the road, v along it)
        float n = 0.0f, amplitude = 0.5f;
        for (int octave = 0; octave < 5; ++octave) {
            const int period = 16 << octave;
            n += valueNoise(u * static_cast<float>(period), v * static_cast<float>(period), octave, period) * amplitude;
            amplitude *= 0.5f;
        }
        const float grit = hashNoise(static_cast<int>(u * 512), static_cast<int>(v * 512), 7);
        float g = 0.06f + 0.05f * n + 0.03f * grit;
        const float lanes[3] = {0.25f, 0.5f, 0.75f};
        for (float lane : lanes)
            if (std::fabs(u - lane) < 0.006f && v < 0.5f) g = 0.75f;   // dashed lane lines
        if (std::fabs(u - 0.03f) < 0.007f || std::fabs(u - 0.97f) < 0.007f) g = 0.8f;  // edge lines
        return {g, g, g * 1.02f};
    }
    case 1: {  // grass-tinted fine checkerboard: a classic aliasing and anisotropy test
        const int cx = static_cast<int>(u * 16), cy = static_cast<int>(v * 16);
        const float check = ((cx + cy) & 1) ? 1.0f : 0.55f;
        const float n = 0.85f + 0.15f * valueNoise(u * 64, v * 64, 3, 64);
        return {0.10f * check * n, 0.22f * check * n, 0.06f * check * n};
    }
    case 2: {  // building facade: window grid
        const float wx = std::fmod(u * 8.0f, 1.0f), wy = std::fmod(v * 8.0f, 1.0f);
        const bool window = wx > 0.2f && wx < 0.8f && wy > 0.25f && wy < 0.8f;
        const float lit = hashNoise(static_cast<int>(u * 8), static_cast<int>(v * 8), 11);
        if (window) return lit > 0.7f ? Rgb{0.9f, 0.8f, 0.55f} : Rgb{0.05f, 0.07f, 0.1f};
        return {0.45f, 0.43f, 0.40f};
    }
    default: {  // test pattern: concentric zone plate and fine lines (moire / shimmer)
        const float x = u - 0.5f, y = v - 0.5f;
        const float zone = 0.5f + 0.5f * std::cos(900.0f * (x * x + y * y));
        const bool line = std::fmod(u * 64.0f, 1.0f) < 0.12f;
        const float g = u < 0.5f ? zone : (line ? 0.95f : 0.08f);
        return {g, g, g};
    }
    }
}

std::vector<std::vector<uint8_t>> buildTextureMips(uint32_t layer) {
    std::vector<std::vector<uint8_t>> mips;
    // Base level, rendered with 2x2 supersampling.
    std::vector<float> linear(kTextureSize * kTextureSize * 3);
    for (uint32_t y = 0; y < kTextureSize; ++y)
        for (uint32_t x = 0; x < kTextureSize; ++x) {
            Rgb sum{0, 0, 0};
            for (int s = 0; s < 4; ++s) {
                const float u = (static_cast<float>(x) + 0.25f + 0.5f * static_cast<float>(s & 1)) / kTextureSize;
                const float v = (static_cast<float>(y) + 0.25f + 0.5f * static_cast<float>(s >> 1)) / kTextureSize;
                const Rgb t = texel(layer, u, v);
                sum.r += t.r * 0.25f; sum.g += t.g * 0.25f; sum.b += t.b * 0.25f;
            }
            float* out = &linear[(y * kTextureSize + x) * 3];
            out[0] = sum.r; out[1] = sum.g; out[2] = sum.b;
        }
    uint32_t size = kTextureSize;
    while (true) {
        std::vector<uint8_t> encoded(static_cast<size_t>(size) * size * 4);
        for (uint32_t i = 0; i < size * size; ++i) {
            for (int c = 0; c < 3; ++c)
                encoded[i * 4 + static_cast<uint32_t>(c)] = static_cast<uint8_t>(std::lround(toSrgb(linear[i * 3 + static_cast<uint32_t>(c)]) * 255.0f));
            encoded[i * 4 + 3] = 255;
        }
        mips.push_back(std::move(encoded));
        if (size == 1) break;
        // Box-filter the next level in linear light.
        const uint32_t next = size / 2;
        std::vector<float> smaller(static_cast<size_t>(next) * next * 3);
        for (uint32_t y = 0; y < next; ++y)
            for (uint32_t x = 0; x < next; ++x)
                for (uint32_t c = 0; c < 3; ++c)
                    smaller[(y * next + x) * 3 + c] = 0.25f * (linear[((2 * y) * size + 2 * x) * 3 + c] +
                        linear[((2 * y) * size + 2 * x + 1) * 3 + c] + linear[((2 * y + 1) * size + 2 * x) * 3 + c] +
                        linear[((2 * y + 1) * size + 2 * x + 1) * 3 + c]);
        linear = std::move(smaller);
        size = next;
    }
    return mips;
}

uint32_t mipCount(uint32_t size) {
    uint32_t count = 1;
    while (size > 1) { size /= 2; ++count; }
    return count;
}

}  // namespace

struct TestScene::Impl {
    const vk::Device& device;
    VkCommandPool pool;
    std::array<Mesh, kMeshCount> meshes{};
    vk::Buffer vertexBuffer, indexBuffer;
    vk::Image albedo;
    vk::Image shadowMap;
    vk::Image environmentSource, environmentFiltered;
    VkSampler shadowCompareSampler = VK_NULL_HANDLE, shadowRawSampler = VK_NULL_HANDLE, environmentSampler = VK_NULL_HANDLE;
    std::array<vk::Buffer, 2> frameBuffers{}, instanceBuffers{};
    static constexpr uint32_t kMaxInstances = 4096;
    vk::Pipeline sky, scene, shadow, envmap, envfilter;
    SceneFormats formats;
    bool prepared = false;

    // Simulation state (advanced only by tick()).
    uint64_t ticks = 0;
    float alpha = 1.0f;
    float stick = 0.0f;
    bool action = false;
    // Settings that change the scene.
    EffectQuality shadows = EffectQuality::High, reflections = EffectQuality::High, skyFog = EffectQuality::High;
    float fov = 50.0f;
    bool animate = true;
    uint32_t shadowSize = 0;
    uint32_t environmentSize = 0;
    EffectQuality builtReflections = EffectQuality::Off;
    bool environmentDirty = true;

    // Camera of the last rendered frame (for motion vectors).
    std::vector<Mat4> previousModels;
    Mat4 previousViewProjection;
    bool havePrevious = false;
    SceneCamera currentCamera;
    Vec3 cameraPosition;
    float renderTime = 0.0f;
    Mat4 lightViewProjection;

    Impl(const vk::Device& device_, VkCommandPool pool_) : device(device_), pool(pool_) {}

    ~Impl() {
        vk::destroy(device, sky); vk::destroy(device, scene); vk::destroy(device, shadow);
        vk::destroy(device, envmap); vk::destroy(device, envfilter);
        vk::destroy(device, vertexBuffer); vk::destroy(device, indexBuffer);
        vk::destroy(device, albedo); vk::destroy(device, shadowMap);
        vk::destroy(device, environmentSource); vk::destroy(device, environmentFiltered);
        for (auto& b : frameBuffers) vk::destroy(device, b);
        for (auto& b : instanceBuffers) vk::destroy(device, b);
        for (VkSampler s : {shadowCompareSampler, shadowRawSampler, environmentSampler})
            if (s) vkDestroySampler(device.device, s, nullptr);
    }

    void initialize() {
        MeshBuilder builder;
        buildBox(builder, meshes[kBox]);
        buildSphere(builder, meshes[kSphere], 32, 64);
        buildCylinder(builder, meshes[kCylinder], 24);
        buildPlane(builder, meshes[kPlane]);
        buildTrack(builder, meshes[kTrack]);
        const VkDeviceSize vertexBytes = builder.vertices.size() * sizeof(Vertex);
        const VkDeviceSize indexBytes = builder.indices.size() * sizeof(uint32_t);
        vertexBuffer = vk::createBuffer(device, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        indexBuffer = vk::createBuffer(device, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        vk::ImageDesc textureDesc;
        textureDesc.format = VK_FORMAT_R8G8B8A8_SRGB;
        textureDesc.extent = {kTextureSize, kTextureSize};
        textureDesc.mips = mipCount(kTextureSize);
        textureDesc.layers = kTextureLayers;
        textureDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        textureDesc.name = "test scene albedo";
        albedo = vk::createImage(device, textureDesc);
        {
            vk::Uploader upload(device, pool);
            upload.copyToBuffer(vertexBuffer, builder.vertices.data(), static_cast<size_t>(vertexBytes));
            upload.copyToBuffer(indexBuffer, builder.indices.data(), static_cast<size_t>(indexBytes));
            vk::Barriers barriers;
            barriers.image(albedo, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, true);
            barriers.flush(upload.command());
            for (uint32_t layer = 0; layer < kTextureLayers; ++layer) {
                const auto mips = buildTextureMips(layer);
                for (uint32_t mip = 0; mip < mips.size(); ++mip) {
                    const uint32_t size = std::max(1u, kTextureSize >> mip);
                    upload.copyToImage(albedo, mips[mip].data(), mips[mip].size(), mip, layer, {size, size});
                }
            }
            vk::toSampled(barriers, albedo, vk::kFragment);
            barriers.flush(upload.command());
            VkMemoryBarrier2 memory{};
            memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memory.srcStageMask = vk::kTransfer;
            memory.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            memory.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            memory.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &memory;
            vkCmdPipelineBarrier2(upload.command(), &dependency);
            upload.submit();
        }
        for (auto& b : frameBuffers) b = vk::createBuffer(device, sizeof(FrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        for (auto& b : instanceBuffers)
            b = vk::createBuffer(device, sizeof(InstanceData) * kMaxInstances, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        shadowCompareSampler = vk::createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                                 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 0.0f, 0.0f, true);
        shadowRawSampler = vk::createSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        environmentSampler = vk::createSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                               VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        envmap = vk::createCompute(device, shaders::envmap_comp(), {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, 32, "envmap");
        envfilter = vk::createCompute(device, shaders::envfilter_comp(),
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, 16, "envfilter");
        ensureShadowMap();
        ensureEnvironment();
    }

    std::vector<VkDescriptorType> sceneTypes() const {
        return {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    }

    void prepare(const SceneFormats& f) {
        vk::destroy(device, sky); vk::destroy(device, scene); vk::destroy(device, shadow);
        formats = f;
        vk::GraphicsDesc desc;
        desc.colors = {f.color, f.velocity};
        desc.depth = f.depth;
        desc.samples = f.samples;
        desc.types = sceneTypes();
        desc.vertex = shaders::fullscreen_vert();
        desc.fragment = shaders::sky_frag();
        desc.name = "sky";
        sky = vk::createGraphics(device, desc);
        desc.vertex = shaders::scene_vert();
        desc.fragment = shaders::scene_frag();
        desc.depthTest = desc.depthWrite = true;
        desc.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
        desc.cull = VK_CULL_MODE_BACK_BIT;
        desc.vertices.stride = sizeof(Vertex);
        desc.vertices.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
                                    {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
        desc.name = "scene";
        scene = vk::createGraphics(device, desc);
        vk::GraphicsDesc depthOnly;
        depthOnly.vertex = shaders::shadow_vert();
        depthOnly.depth = VK_FORMAT_D32_SFLOAT;
        depthOnly.depthTest = depthOnly.depthWrite = true;
        depthOnly.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthOnly.depthBias = true;
        depthOnly.cull = VK_CULL_MODE_NONE;
        depthOnly.vertices.stride = sizeof(Vertex);
        depthOnly.vertices.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};
        depthOnly.types = sceneTypes();
        depthOnly.name = "shadow";
        shadow = vk::createGraphics(device, depthOnly);
        prepared = true;
    }

    uint32_t wantedShadowSize() const {
        switch (shadows) {
        case EffectQuality::Low: return 1024;
        case EffectQuality::Medium: case EffectQuality::High: return 2048;
        case EffectQuality::Ultra: return 4096;
        default: return 0;
        }
    }

    void ensureShadowMap() {
        const uint32_t size = std::max(wantedShadowSize(), 16u);
        if (shadowMap.valid() && shadowSize == size) return;
        if (shadowMap.valid()) { vkDeviceWaitIdle(device.device); vk::destroy(device, shadowMap); }
        vk::ImageDesc desc;
        desc.format = VK_FORMAT_D32_SFLOAT;
        desc.extent = {size, size};
        desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.name = "shadow map";
        shadowMap = vk::createImage(device, desc);
        shadowSize = size;
        std::printf("Test scene: shadow map %ux%u\n", size, size);
    }

    void ensureEnvironment() {
        uint32_t size = 128;
        switch (reflections) {
        case EffectQuality::Low: size = 64; break;
        case EffectQuality::Medium: size = 128; break;
        case EffectQuality::High: size = 256; break;
        case EffectQuality::Ultra: size = 512; break;
        default: size = 128; break;
        }
        if (environmentSource.valid() && environmentSize == size && builtReflections == reflections) return;
        if (environmentSource.valid()) {
            vkDeviceWaitIdle(device.device);
            vk::destroy(device, environmentSource);
            vk::destroy(device, environmentFiltered);
        }
        vk::ImageDesc desc;
        desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.extent = {size, size};
        desc.mips = mipCount(size);
        desc.cube = true;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        desc.name = "environment source";
        environmentSource = vk::createImage(device, desc);
        desc.mips = std::min(desc.mips, 6u);
        desc.name = "environment prefiltered";
        environmentFiltered = vk::createImage(device, desc);
        environmentSize = size;
        builtReflections = reflections;
        environmentDirty = true;
        std::printf("Test scene: environment cube map %ux%u\n", size, size);
    }

    Vec3 sunDirection() const { return normalize(Vec3{0.45f, 0.55f, -0.7f}); }

    void generateEnvironment(VkCommandBuffer command) {
        vk::beginLabel(command, "environment map");
        vk::Barriers barriers;
        vk::toStorage(barriers, environmentSource, true);
        barriers.flush(command);
        vk::bind(command, envmap);
        struct { Vec4 sun; float size; float sunScale; float pad[2]; } push{};
        push.sun = Vec4(sunDirection(), 0.0f);
        push.sunScale = 1.0f;
        for (uint32_t mip = 0; mip < environmentSource.mips; ++mip) {
            const uint32_t size = std::max(1u, environmentSize >> mip);
            push.size = static_cast<float>(size);
            vk::Descriptors().storage(0, environmentSource.mipViews[mip]).push(command, envmap);
            vk::pushConstants(command, envmap, push);
            vkCmdDispatch(command, (size + 7) / 8, (size + 7) / 8, 6);
        }
        vk::toSampled(barriers, environmentSource, vk::kCompute | vk::kFragment);
        vk::toStorage(barriers, environmentFiltered, true);
        barriers.flush(command);
        vk::bind(command, envfilter);
        uint32_t samples = 64;
        switch (reflections) {
        case EffectQuality::Low: samples = 32; break;
        case EffectQuality::Medium: samples = 64; break;
        case EffectQuality::High: samples = 128; break;
        case EffectQuality::Ultra: samples = 256; break;
        default: break;
        }
        for (uint32_t mip = 0; mip < environmentFiltered.mips; ++mip) {
            const uint32_t size = std::max(1u, environmentSize >> mip);
            struct { float size, roughness, sourceSize; uint32_t samples; } filter{
                static_cast<float>(size), static_cast<float>(mip) / static_cast<float>(environmentFiltered.mips - 1),
                static_cast<float>(environmentSize), samples};
            vk::Descriptors().sampled(0, environmentSource.view, environmentSampler)
                .storage(1, environmentFiltered.mipViews[mip]).push(command, envfilter);
            vk::pushConstants(command, envfilter, filter);
            vkCmdDispatch(command, (size + 7) / 8, (size + 7) / 8, 6);
        }
        vk::toSampled(barriers, environmentFiltered, vk::kFragment);
        barriers.flush(command);
        vk::endLabel(command);
        environmentDirty = false;
    }

    // --- Animation (pure functions of time) ---
    struct Car { Vec3 position; float heading; float speed; float wheelAngle; Vec4 color; };
    Car car(int index, float time) const {
        const float lanes[4] = {-5.4f, -1.8f, 1.8f, 5.4f};
        const float radius = kTrackRadius + lanes[index % 4];
        const float speed = 30.0f + 3.0f * static_cast<float>(index % 3) - (index == 0 ? 2.0f : 0.0f);  // m/s
        const float start = static_cast<float>(index) * 0.35f;
        const float angle = start + speed / radius * time;
        Car c;
        c.position = {std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
        c.heading = angle;
        c.speed = speed;
        c.wheelAngle = speed * time / 0.33f;
        const Vec4 colors[kCars] = {{0.55f, 0.02f, 0.02f, 1.0f}, {0.02f, 0.10f, 0.45f, 1.0f}, {0.85f, 0.85f, 0.82f, 1.0f},
                                    {0.02f, 0.02f, 0.02f, 1.0f}, {0.90f, 0.45f, 0.02f, 1.0f}, {0.10f, 0.35f, 0.12f, 1.0f}};
        c.color = colors[index % kCars];
        return c;
    }

    void addCar(std::vector<Draw>& list, const Car& c) const {
        // Local frame: +X forward along the track tangent, +Z to the outside.
        const Vec3 forward{-std::sin(c.heading), 0.0f, std::cos(c.heading)};
        const float yaw = std::atan2(forward.x, forward.z);   // rotate local +Z to forward
        const Mat4 base = translate(c.position) * rotateY(yaw);
        // Body: base coat under a clear coat (color.w = clear-coat amount).
        list.push_back({kBox, base * translate({0, 0.55f, 0}) * scale({1.8f, 0.6f, 4.4f}), c.color, {0.35f, 0.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
        list.push_back({kBox, base * translate({0, 1.05f, -0.3f}) * scale({1.5f, 0.45f, 2.0f}), {0.03f, 0.03f, 0.04f, 1.0f}, {0.05f, 0.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
        for (int w = 0; w < 4; ++w) {
            const float x = (w & 1) ? 0.85f : -0.85f, z = (w & 2) ? 1.4f : -1.35f;
            list.push_back({kCylinder, base * translate({x, 0.33f, z}) * rotateX(c.wheelAngle) * rotateZ(kPi * 0.5f) *
                             scale({0.66f, 0.28f, 0.66f}), {0.02f, 0.02f, 0.02f, 0.0f}, {0.8f, 0.0f, 0.0f, 1.0f}, {1, 1, 0, 0}});
        }
        // Head and tail lights: small, very bright emitters (bloom).
        for (int side = -1; side <= 1; side += 2) {
            list.push_back({kBox, base * translate({0.6f * static_cast<float>(side), 0.65f, 2.21f}) * scale({0.35f, 0.12f, 0.02f}),
                             {1.0f, 0.95f, 0.85f, 0.0f}, {0.2f, 0.0f, 40.0f, -1.0f}, {1, 1, 0, 0}});
            list.push_back({kBox, base * translate({0.6f * static_cast<float>(side), 0.7f, -2.21f}) * scale({0.35f, 0.1f, 0.02f}),
                             {1.0f, 0.05f, 0.02f, 0.0f}, {0.2f, 0.0f, 25.0f, -1.0f}, {1, 1, 0, 0}});
        }
    }

    void buildDraws(std::vector<Draw>& list, float time) const {
        list.clear();
        // Ground (grass checkerboard), large so the horizon is far away.
        list.push_back({kPlane, scale({1200, 1, 1200}), {1, 1, 1, 0}, {0.9f, 0.0f, 0.0f, 1.0f}, {300, 300, 0, 0}});
        list.push_back({kTrack, Mat4{}, {1, 1, 1, 0}, {0.7f, 0.0f, 0.0f, 0.0f}, {1, 1, 0, 0}});
        // Fences: thin posts and wires along both edges (sub-pixel geometry at distance).
        const float circumference = 2.0f * kPi * kTrackRadius;
        const int posts = static_cast<int>(circumference / 2.5f);
        for (int side = 0; side < 2; ++side) {
            const float r = kTrackRadius + (side ? 1.0f : -1.0f) * (kTrackWidth * 0.5f + 1.5f);
            for (int i = 0; i < posts; ++i) {
                const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(posts);
                list.push_back({kBox, translate({std::cos(a) * r, 0.6f, std::sin(a) * r}) * rotateY(-a) * scale({0.06f, 1.2f, 0.06f}),
                                 {0.6f, 0.6f, 0.62f, 0.0f}, {0.35f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
            }
            // Wires: short straight segments between posts at three heights.
            for (int i = 0; i < posts; ++i) {
                const float a = 2.0f * kPi * (static_cast<float>(i) + 0.5f) / static_cast<float>(posts);
                const float chord = 2.0f * r * std::sin(kPi / static_cast<float>(posts));
                for (float h : {0.4f, 0.8f, 1.15f})
                    list.push_back({kBox, translate({std::cos(a) * r, h, std::sin(a) * r}) * rotateY(-a) * scale({0.015f, 0.015f, chord}),
                                     {0.7f, 0.7f, 0.72f, 0.0f}, {0.3f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
            }
        }
        // Gantries over the track: trusses of thin members with bright lamps.
        for (int g = 0; g < 4; ++g) {
            const float a = kPi * 0.5f * static_cast<float>(g) + 0.4f;
            const Mat4 frame = translate({std::cos(a) * kTrackRadius, 0, std::sin(a) * kTrackRadius}) * rotateY(-a);
            for (int side = -1; side <= 1; side += 2)
                list.push_back({kBox, frame * translate({static_cast<float>(side) * 10.0f, 4.0f, 0}) * scale({0.3f, 8.0f, 0.3f}),
                                 {0.5f, 0.5f, 0.52f, 0.0f}, {0.4f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
            for (float h : {7.2f, 8.0f})
                list.push_back({kBox, frame * translate({0, h, 0}) * scale({20.0f, 0.08f, 0.08f}),
                                 {0.5f, 0.5f, 0.52f, 0.0f}, {0.4f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
            for (int d = 0; d < 20; ++d) {
                const float x = -9.5f + static_cast<float>(d);
                list.push_back({kBox, frame * translate({x, 7.6f, 0}) * rotateZ((d & 1) ? 0.785f : -0.785f) * scale({0.04f, 1.13f, 0.04f}),
                                 {0.5f, 0.5f, 0.52f, 0.0f}, {0.4f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
            }
            for (int l = 0; l < 4; ++l)
                list.push_back({kBox, frame * translate({-6.0f + 4.0f * static_cast<float>(l), 7.0f, 0.3f}) * scale({1.2f, 0.25f, 0.1f}),
                                 {1.0f, 0.97f, 0.9f, 0.0f}, {0.3f, 0.0f, 120.0f, -1.0f}, {1, 1, 0, 0}});
        }
        // Light poles with bright bulbs around the outside of the track.
        for (int i = 0; i < 24; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / 24.0f;
            const float r = kTrackRadius + kTrackWidth * 0.5f + 5.0f;
            list.push_back({kCylinder, translate({std::cos(a) * r, 5.0f, std::sin(a) * r}) * scale({0.25f, 10.0f, 0.25f}),
                             {0.4f, 0.4f, 0.42f, 0.0f}, {0.5f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
            list.push_back({kSphere, translate({std::cos(a) * (r - 1.0f), 10.2f, std::sin(a) * (r - 1.0f)}) * scale({0.6f, 0.6f, 0.6f}),
                             {1.0f, 0.9f, 0.7f, 0.0f}, {0.2f, 0.0f, 200.0f, -1.0f}, {1, 1, 0, 0}});
        }
        // Buildings outside the track (depth range, facade texture).
        for (int i = 0; i < 28; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / 28.0f + 0.05f;
            const float r = 180.0f + 60.0f * hashNoise(i, 1, 5);
            const float h = 15.0f + 50.0f * hashNoise(i, 2, 5);
            const float w = 16.0f + 14.0f * hashNoise(i, 3, 5);
            list.push_back({kBox, translate({std::cos(a) * r, h * 0.5f, std::sin(a) * r}) * rotateY(-a) * scale({w, h, w}),
                             {0.9f, 0.9f, 0.9f, 0.0f}, {0.6f, 0.0f, 0.0f, 2.0f}, {std::max(1.0f, w / 8.0f), std::max(1.0f, h / 8.0f), 0, 0}});
        }
        // Material spheres (roughness 0..1) and a test-pattern billboard inside the track.
        for (int i = 0; i < 7; ++i) {
            const float a = 0.9f + 0.035f * static_cast<float>(i);
            const float r = kTrackRadius - kTrackWidth * 0.5f - 5.0f;
            list.push_back({kSphere, translate({std::cos(a) * r, 1.0f, std::sin(a) * r}) * scale({2.0f, 2.0f, 2.0f}),
                             {0.95f, 0.75f, 0.35f, 0.0f}, {static_cast<float>(i) / 6.0f, 1.0f, 0.0f, -1.0f}, {1, 1, 0, 0}});
        }
        for (int b = 0; b < 3; ++b) {
            const float a = 2.2f + 2.0f * static_cast<float>(b);
            const float r = kTrackRadius - kTrackWidth * 0.5f - 4.0f;
            list.push_back({kBox, translate({std::cos(a) * r, 3.0f, std::sin(a) * r}) * rotateY(-a + kPi * 0.5f) * scale({12.0f, 4.0f, 0.2f}),
                             {1.0f, 1.0f, 1.0f, 0.0f}, {0.5f, 0.0f, 0.0f, 3.0f}, {1, 1, 0, 0}});
        }
        for (int i = 0; i < kCars; ++i) addCar(list, car(i, time));
        // Group by mesh for instanced draws (stable, so instance order is the same every frame).
        std::stable_sort(list.begin(), list.end(), [](const Draw& a, const Draw& b) { return a.mesh < b.mesh; });
    }

    void updateCamera(float time, const SceneView& view) {
        const Car target = car(0, time);
        const Vec3 forward{-std::sin(target.heading), 0.0f, std::cos(target.heading)};
        const Vec3 outward{std::cos(target.heading), 0.0f, std::sin(target.heading)};
        // Chase camera; the stick swings it around the car (up to +-70 degrees).
        const float swing = stick * 1.2f;
        const Vec3 back = forward * -std::cos(swing) + outward * std::sin(swing);
        cameraPosition = target.position + back * 7.5f + Vec3{0, 2.1f, 0};
        const Vec3 look = target.position + forward * 6.0f + Vec3{0, 1.0f, 0};
        SceneCamera camera;
        camera.view = lookAt(cameraPosition, look, {0, 1, 0});
        camera.verticalFov = fov * kPi / 180.0f;
        camera.near = 0.1f;
        camera.projection = perspectiveReverseZ(camera.verticalFov, view.aspect, camera.near);
        const Mat4 viewProjection = camera.projection * camera.view;
        camera.previousViewProjection = havePrevious ? previousViewProjection : viewProjection;
        camera.cut = !havePrevious;
        currentCamera = camera;
    }

    void updateLight() {
        // Orthographic sun shadow around the camera, snapped to shadow texels so it does not shimmer.
        const Vec3 sun = sunDirection();
        const float radius = shadows == EffectQuality::Ultra ? 90.0f : 70.0f;
        const Mat4 lightView = lookAt(Vec3{0, 0, 0}, -sun, {0, 1, 0});
        const Vec4 center = lightView * Vec4(cameraPosition, 1.0f);
        const float texel = 2.0f * radius / static_cast<float>(std::max(shadowSize, 1u));
        const float cx = std::floor(center.x / texel) * texel, cy = std::floor(center.y / texel) * texel;
        const Mat4 projection = orthographic(cx - radius, cx + radius, cy - radius, cy + radius, -400.0f, 400.0f);
        lightViewProjection = projection * lightView;
    }

    void writeFrame(const SceneView& view, const std::vector<Draw>& list) {
        auto* instances = static_cast<InstanceData*>(instanceBuffers[view.slot].mapped);
        if (previousModels.size() != list.size()) {
            previousModels.resize(list.size());
            for (size_t i = 0; i < list.size(); ++i) previousModels[i] = list[i].model;
        }
        const size_t count = std::min<size_t>(list.size(), kMaxInstances);
        for (size_t i = 0; i < count; ++i) {
            instances[i] = {list[i].model, havePrevious ? previousModels[i] : list[i].model, list[i].color, list[i].material, list[i].uv};
            previousModels[i] = list[i].model;
        }
        FrameData frame{};
        Mat4 jitter = translate({view.jitterClip.x, view.jitterClip.y, 0.0f});
        const Mat4 viewProjection = currentCamera.projection * currentCamera.view;
        frame.viewProjection = jitter * viewProjection;
        frame.viewProjectionUnjittered = viewProjection;
        frame.previousViewProjection = currentCamera.previousViewProjection;
        frame.inverseViewProjection = inverse(viewProjection);
        frame.lightViewProjection = lightViewProjection;
        frame.cameraPosition = Vec4(cameraPosition, renderTime);
        // Sun about eight times brighter than the sky dome (a clear afternoon), light haze.
        frame.sunDirection = Vec4(sunDirection(), 6.0f);
        frame.sunColor = Vec4(1.0f, 0.95f, 0.88f, 1.0f);
        int fogMode = 2;
        float density = 0.0035f;
        if (skyFog == EffectQuality::Original) fogMode = 0;
        else if (skyFog == EffectQuality::Low) fogMode = 1;
        else if (skyFog == EffectQuality::Off) density = 0.0f;
        frame.fogParams = Vec4(density, 0.08f, 0.0f, static_cast<float>(fogMode));
        frame.fogColor = Vec4(0.62f, 0.70f, 0.82f, skyFog == EffectQuality::Off ? 1e9f : 600.0f);
        int shadowMode = 3;
        switch (shadows) {
        case EffectQuality::Original: case EffectQuality::Off: shadowMode = 0; break;
        case EffectQuality::Low: shadowMode = 1; break;
        case EffectQuality::Medium: shadowMode = 2; break;
        case EffectQuality::High: shadowMode = 3; break;
        case EffectQuality::Ultra: shadowMode = 4; break;
        }
        frame.shadowParams = Vec4(static_cast<float>(shadowMode), 1.0f / static_cast<float>(std::max(shadowSize, 1u)), 0.0004f, 0.6f);
        const bool sharpReflections = reflections == EffectQuality::Original || reflections == EffectQuality::Off;
        frame.envParams = Vec4(static_cast<float>(environmentFiltered.mips), sharpReflections ? 0.0f : 1.0f,
                               reflections == EffectQuality::Off ? 0.6f : 1.0f, 0.0f);
        frame.renderInfo = Vec4(static_cast<float>(view.renderExtent.width), static_cast<float>(view.renderExtent.height),
                                static_cast<float>(view.frameIndex), action ? 1.0f : 0.0f);
        frame.jitter = Vec4(view.jitterPixels.x, view.jitterPixels.y, 0.0f, 0.0f);
        // Blob shadows (the "original" shadow mode) under the cars.
        for (int i = 0; i < kCars && i < 8; ++i) {
            const Car c = car(i, renderTime);
            frame.blobs[i] = Vec4(c.position.x, c.position.z, 1.4f, shadows == EffectQuality::Off ? 0.0f : 0.65f);
        }
        std::memcpy(frameBuffers[view.slot].mapped, &frame, sizeof(frame));
    }

    vk::Descriptors descriptors(const SceneView& view) const {
        vk::Descriptors d;
        const bool sharp = reflections == EffectQuality::Original || reflections == EffectQuality::Off;
        d.uniform(0, frameBuffers[view.slot].buffer, 0, sizeof(FrameData))
         .storageBuffer(1, instanceBuffers[view.slot].buffer, 0, VK_WHOLE_SIZE)
         .sampled(2, albedo.view, view.materialSampler)
         .sampled(3, shadowMap.view, shadowCompareSampler)
         .sampled(4, shadowMap.view, shadowRawSampler)
         .sampled(5, sharp ? environmentSource.view : environmentFiltered.view, environmentSampler);
        return d;
    }

    std::vector<Draw> draws;

    void recordPrepass(VkCommandBuffer command, const SceneView& view) {
        ensureShadowMap();
        ensureEnvironment();
        if (environmentDirty) generateEnvironment(command);
        renderTime = (static_cast<float>(ticks) - 1.0f + alpha) * kTickSeconds;
        if (renderTime < 0.0f) renderTime = 0.0f;
        updateCamera(renderTime, view);
        updateLight();
        buildDraws(draws, renderTime);
        writeFrame(view, draws);
        vk::Barriers barriers;
        const bool shadowPass = shadowMode() > 0;
        if (shadowPass) {
            vk::beginLabel(command, "shadow map");
            vk::toDepth(barriers, shadowMap, true);
            barriers.flush(command);
            VkRenderingAttachmentInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = shadowMap.view;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering.renderArea.extent = {shadowSize, shadowSize};
            rendering.layerCount = 1;
            rendering.pDepthAttachment = &depth;
            vkCmdBeginRendering(command, &rendering);
            setViewport(command, {shadowSize, shadowSize});
            vkCmdSetDepthBias(command, 1.25f, 0.0f, 1.75f);
            vk::bind(command, shadow);
            descriptors(view).push(command, shadow);
            drawAll(command);
            vkCmdEndRendering(command);
            vk::endLabel(command);
        }
        vk::toSampled(barriers, shadowMap, vk::kFragment);
        barriers.flush(command);
    }

    int shadowMode() const {
        return (shadows == EffectQuality::Original || shadows == EffectQuality::Off) ? 0 : 1;
    }

    static void setViewport(VkCommandBuffer command, VkExtent2D extent) {
        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
    }

    void drawAll(VkCommandBuffer command) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer.buffer, &offset);
        vkCmdBindIndexBuffer(command, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        const uint32_t count = static_cast<uint32_t>(std::min<size_t>(draws.size(), kMaxInstances));
        uint32_t first = 0;
        while (first < count) {
            uint32_t last = first;
            while (last < count && draws[last].mesh == draws[first].mesh) ++last;
            const Mesh& mesh = meshes[draws[first].mesh];
            vkCmdDrawIndexed(command, mesh.indexCount, last - first, mesh.firstIndex, mesh.vertexOffset, first);
            first = last;
        }
    }

    void recordMain(VkCommandBuffer command, const SceneView& view) {
        setViewport(command, view.renderExtent);
        vk::bind(command, sky);
        descriptors(view).push(command, sky);
        vkCmdDraw(command, 3, 1, 0, 0);
        vk::bind(command, scene);
        descriptors(view).push(command, scene);
        drawAll(command);
        previousViewProjection = currentCamera.projection * currentCamera.view;
        havePrevious = true;
    }
};

TestScene::TestScene(const vk::Device& device, VkCommandPool pool) : impl(std::make_unique<Impl>(device, pool)) {
    impl->initialize();
}
TestScene::~TestScene() = default;
void TestScene::tick() { if (impl->animate) ++impl->ticks; }
void TestScene::setInterpolation(float alpha) { impl->alpha = std::clamp(alpha, 0.0f, 1.0f); }
void TestScene::setControls(float stick, bool action) { impl->stick = std::clamp(stick, -1.0f, 1.0f); impl->action = action; }
void TestScene::apply(const Settings& settings) {
    impl->shadows = settings.shadows;
    impl->reflections = settings.reflections;
    impl->skyFog = settings.skyFog;
    impl->fov = settings.fov;
    impl->animate = settings.animate;
}
float TestScene::speedKmh() const { return impl->car(0, impl->renderTime).speed * 3.6f; }
int TestScene::lap() const {
    const float distance = impl->car(0, impl->renderTime).speed * impl->renderTime;
    return 1 + static_cast<int>(distance / (2.0f * kPi * kTrackRadius));
}
void TestScene::prepare(const SceneFormats& formats) { impl->prepare(formats); }
void TestScene::recordPrepass(VkCommandBuffer command, const SceneView& view) { impl->recordPrepass(command, view); }
void TestScene::recordMain(VkCommandBuffer command, const SceneView& view) { impl->recordMain(command, view); }
SceneCamera TestScene::camera() const { return impl->currentCamera; }

}  // namespace gt4
