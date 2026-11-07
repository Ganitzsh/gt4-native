// The mesh cache (platform/scene/mesh_cache.h, ticket B5) against GT4's CPU 3D renderers: captures replayed through
// the graphics stream, and for every batch handed to the GPU, its decoded object-space vertices transformed by the
// batch's own constants must land where the CPU renderers (platform/gs/renderers3d.cpp) put them, within 1/16 pixel
// (one unit of the GS's 12.4 coordinates), with the same triangles. Then the frame's batches, grouped into objects,
// fill the cache on its workers: decode time, memory and welding reported.
//   gt4_mesh_cache_test CAPTURE...   (captures are game data: skipped (77) when none can be read)
#include "mesh_cache.h"

#include "link_host.h"
#include "renderers3d.h"
#include "stream.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace gt4;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// The CPU renderers' arithmetic for a point (renderers3d.cpp Projector::clip, projected, window and FTOI4).
float saturate(float f) {
    if (f != f) return 0.0f;
    return std::clamp(f, -FLT_MAX, FLT_MAX);
}
float reciprocal(float w) {
    if (w == 0.0f) return std::signbit(w) ? -FLT_MAX : FLT_MAX;
    return saturate(1.0f / w);
}
uint32_t fixed4(float v) {
    if (v != v) return 0;
    const double scaled = std::trunc(static_cast<double>(v) * 16.0);
    if (scaled >= 2147483647.0) return 0x7FFFFFFFu;
    if (scaled <= -2147483648.0) return 0x80000000u;
    return static_cast<uint32_t>(static_cast<int32_t>(scaled));
}
void windowXY(const gfx::ModelJob& job, const float p[3], uint32_t& x, uint32_t& y) {
    float clip[4];
    for (int i = 0; i < 4; ++i) clip[i] = saturate(job.matrix[0][i] * p[0] + job.matrix[1][i] * p[1] + job.matrix[2][i] * p[2] + job.matrix[3][i]);
    const float q = reciprocal(clip[3]);
    const float sx = saturate(std::min(clip[0], job.clamp[0]) * q), sy = saturate(std::min(clip[1], job.clamp[1]) * q);
    x = fixed4(job.offset[0] + sx * job.scale[0]) & 0xFFFF;
    y = fixed4(job.offset[1] + sy * job.scale[1]) & 0xFFFF;
}
// The point's window x, y in pixels (before FTOI4); false when it is at or behind the eye (w <= 0).
bool windowXYW(const gfx::ModelJob& job, const float p[3], float& x, float& y) {
    float clip[4];
    for (int i = 0; i < 4; ++i) clip[i] = saturate(job.matrix[0][i] * p[0] + job.matrix[1][i] * p[1] + job.matrix[2][i] * p[2] + job.matrix[3][i]);
    if (!(clip[3] > 1e-6f)) return false;
    const float q = reciprocal(clip[3]);
    x = job.offset[0] + saturate(std::min(clip[0], job.clamp[0]) * q) * job.scale[0];
    y = job.offset[1] + saturate(std::min(clip[1], job.clamp[1]) * q) * job.scale[1];
    return std::isfinite(x) && std::isfinite(y);
}
uint32_t distance16(uint32_t a, uint32_t b) {
    const uint32_t d = (a - b) & 0xFFFF;
    return std::min(d, 0x10000u - d);
}

// The batch's own vertices from the CPU renderers (the last primitive run of the batch).
class Collect : public ps2::PrimitiveSink {
public:
    std::vector<ps2::GsVertex> vertices;
    void packet(const uint8_t*, size_t) override {}
    void primitives(const ps2::GsPrimitives& p) override { vertices.assign(p.vertices, p.vertices + p.count); }
};

class Target : public ps2::GsTarget {
public:
    ps2::Renderers3d reference;
    scene::MeshCache& cache;
    std::vector<scene::BatchCopy> frame;   // this frame's batches, in order
    uint64_t batches = 0, verticesChecked = 0, verticesOff = 0, worstUnits = 0, trianglesMesh = 0, trianglesCpu = 0, displays = 0;
    uint64_t batchesOff = 0, startsWithoutAdc = 0, objects = 0;
    uint64_t stChecked = 0, stOff = 0, coloursChecked = 0, coloursOff = 0;
    uint64_t facing[3][2][2] = {};   // [renderer 1 or 2][kept, culled][positive area, negative area]
    struct FrameCost { uint64_t decoded, bytes; };
    std::vector<FrameCost> frames;   // meshes and bytes decoded at each frame's end (the steady state)
    scene::MeshCache::Stats lastStats{};
    explicit Target(scene::MeshCache& c) : cache(c) {}

    bool models() const override { return true; }
    void model(const gfx::ModelJob& job, const uint32_t* words) override {
        ++batches;
        scene::BatchCopy copy;
        copy.job = job;
        std::memcpy(copy.words, words, sizeof copy.words);
        frame.push_back(copy);

        // The CPU renderers on the same batch: its constants set, entered after the setup, no header.
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 4; ++i) reference.constants.matrix[k][i] = job.matrix[k][i];
        for (int i = 0; i < 4; ++i) {
            reference.constants.scale[i] = job.scale[i];
            reference.constants.offset[i] = job.offset[i];
            reference.constants.clamp[i] = job.clamp[i];
            reference.constants.guard[i] = job.guard[i];
        }
        const bool lit = job.chain & 1;
        const uint32_t group = (lit ? 12u : 0u) + job.renderer * 2 + 1;   // odd: no header
        ps2::BatchBuffer buffer;
        std::memcpy(buffer.words, words, sizeof buffer.words);
        ps2::Batch3d batch;
        batch.entry = (0x1B08 + group * 0x28 + 8) / 8;   // one instruction in: past the setup's call
        batch.buffer = &buffer;
        if (lit) {
            const uint32_t body = job.chain >> 16, unpack = (job.chain >> 4) & 0xF;
            batch.itop = (body - 0x50 + unpack * 0x10) / 8;
        }
        Collect cpu;
        uint32_t continueAt = 0;
        reference.run(batch, cpu, continueAt);

        const scene::DecodedBatch decoded = scene::decodeBatch(job, words);
        bool off = decoded.vertices.size() != cpu.vertices.size();
        const size_t n = std::min(decoded.vertices.size(), cpu.vertices.size());
        for (size_t k = 0; k < n; ++k) {
            uint32_t x, y;
            windowXY(job, decoded.vertices[k].position, x, y);
            const uint32_t units = std::max(distance16(x, cpu.vertices[k].x & 0xFFFF), distance16(y, cpu.vertices[k].y & 0xFFFF));
            worstUnits = std::max<uint64_t>(worstUnits, units);
            ++verticesChecked;
            if (units > 1) ++verticesOff, off = true;
            if (decoded.starts[k] && !cpu.vertices[k].adc) ++startsWithoutAdc, off = true;
        }
        // Texture coordinates where the batch's own reach the GS (S/Q and T/Q over the CPU renderers' Q): textured,
        // and no body that computes them per frame (0x10A8, 0x12C8, 0x1608) or keeps normals in the ST slot (0x1908).
        // Colours: the given words, for unlit batches (a lighting body computes them per frame).
        const uint32_t body = lit ? job.chain >> 16 : 0, unpack = lit ? (job.chain >> 4) & 0xF : 5;
        const bool ownSt = decoded.texture && body != 0x10A8 && body != 0x12C8 && body != 0x1608 && unpack != 0;
        for (size_t k = 0; k < n; ++k) {
            const ps2::GsVertex& c = cpu.vertices[k];
            const scene::MeshVertex& m = decoded.vertices[k];
            if (ownSt && c.q != 0.0f && std::fabs(c.q) < 1e30f && std::isfinite(c.s) && std::isfinite(c.t)) {
                for (int i = 0; i < 2; ++i) {
                    const float fromCpu = (i ? c.t : c.s) / c.q;
                    ++stChecked;
                    if (std::fabs(fromCpu - m.st[i]) > 1.0f / 4096.0f + 1e-5f * std::fabs(m.st[i])) ++stOff, off = true;
                }
            }
            if (!lit) {
                ++coloursChecked;
                if (c.rgba != m.colour) ++coloursOff, off = true;
            }
        }
        // Winding: for the culling renderers (0x2058, 0x2070), the triangles the CPU renderers keep must all turn one
        // way in the mesh's single winding, and the ones they cull the other (a count match would hide a strip turned
        // over). Triangles with a vertex at or behind the eye, or nearly no area, are left out.
        if (job.renderer == 1 || job.renderer == 2) {
            size_t t = 0;
            for (size_t k = 0; k < n && t + 2 < decoded.triangles.size(); ++k) {
                if (decoded.starts[k]) continue;
                float wx[3], wy[3];
                bool front = true;
                for (int v = 0; v < 3; ++v) front = windowXYW(job, decoded.vertices[decoded.triangles[t + v]].position, wx[v], wy[v]) && front;
                t += 3;
                if (!front) continue;
                const float area = (wx[1] - wx[0]) * (wy[2] - wy[0]) - (wx[2] - wx[0]) * (wy[1] - wy[0]);
                if (std::fabs(area) < 0.01f) continue;
                facing[job.renderer][cpu.vertices[k].adc ? 1 : 0][area > 0.0f ? 0 : 1]++;
            }
        }
        // Triangles: every vertex but the strips' first two completes one.
        uint64_t cpuTriangles = 0;
        for (size_t k = 0; k < n; ++k) cpuTriangles += decoded.starts[k] ? 0 : 1;
        cpuTriangles += cpu.vertices.size() > n ? cpu.vertices.size() - n : 0;
        trianglesCpu += cpuTriangles;
        trianglesMesh += decoded.triangles.size() / 3;
        if (decoded.triangles.size() / 3 != cpuTriangles) off = true;
        if (off && batchesOff++ < 5)
            std::fprintf(stderr, "batch %llu: renderer %u chain %08X count %u: %zu decoded / %zu CPU vertices, worst %llu units\n",
                         static_cast<unsigned long long>(batches), job.renderer, job.chain, job.count, decoded.vertices.size(),
                         cpu.vertices.size(), static_cast<unsigned long long>(worstUnits));
    }
    void display(const gfx::DisplayRecord&) override { ++displays; }
    void vsync(uint32_t, uint32_t) override {
        // The frame's objects: runs of jobs with the same transform (until instance records), each one mesh.
        std::vector<gfx::ModelJob> jobs;
        for (const auto& b : frame) jobs.push_back(b.job);
        for (const auto& [first, count] : scene::groupByMatrix(jobs.data(), jobs.size())) {
            std::vector<scene::BatchCopy> items(frame.begin() + first, frame.begin() + first + count);
            cache.request(items);
            ++objects;
        }
        frame.clear();
        // What this frame decoded (waited for here, so it counts in its own frame), then its end: eviction.
        const scene::MeshCache::Stats before = lastStats;
        cache.wait();
        lastStats = cache.stats();
        frames.push_back({lastStats.decoded - before.decoded, lastStats.decodedBytes - before.decodedBytes});
        cache.endFrame();
    }
    void readTransfer(void* out, uint32_t qwc) override { std::memset(out, 0, size_t(qwc) * 16); }
    void gif(uint32_t, const uint8_t*, size_t) override {}
};

}  // namespace

int main(int argc, char** argv) {
    // Unit checks on a synthetic batch: one strip of 4 vertices, integer positions (0x1810), ST in 4.12.
    {
        gfx::ModelJob job{};
        job.count = job.nloop = 4;
        job.chain = 1u | 2u << 4 | 0xB48u << 16;
        job.prim = 0x14;
        uint32_t words[gfx::kModelWords] = {};
        const int32_t xyz[4][3] = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, -5}};
        for (int k = 0; k < 4; ++k) {
            for (int i = 0; i < 3; ++i) words[k * 4 + i] = static_cast<uint32_t>(xyz[k][i]);
            words[(64 + k) * 4 + 0] = static_cast<uint32_t>(k * 4096);   // s = k
            words[(64 + k) * 4 + 1] = 2048;                                // t = 0.5
        }
        words[64 * 4 + 3] = 1;        // one strip
        words[65 * 4 + 3] = (4 - 2) * 3;   // of four vertices
        const scene::DecodedBatch b = scene::decodeBatch(job, words);
        check(b.vertices.size() == 4 && b.vertices[3].position[0] == 10.0f && b.vertices[3].position[2] == -5.0f, "decode: integer positions");
        check(b.vertices[2].st[0] == 2.0f && b.vertices[2].st[1] == 0.5f, "decode: ST in 4.12");
        check(b.triangles.size() == 6 && b.triangles[0] == 0 && b.triangles[1] == 1 && b.triangles[2] == 2 && b.triangles[3] == 2 &&
                  b.triangles[4] == 1 && b.triangles[5] == 3,
              "decode: two triangles, the second turned to the first's winding");
        check(b.starts[0] && b.starts[1] && !b.starts[2], "decode: the strip's first two vertices start no triangle");
        // Two copies of the batch welded: the same four vertices.
        std::vector<scene::BatchCopy> items(2);
        items[0].job = items[1].job = job;
        std::memcpy(items[0].words, words, sizeof words);
        std::memcpy(items[1].words, words, sizeof words);
        const scene::Mesh mesh = scene::buildMesh(items);
        check(mesh.vertices.size() == 4 && mesh.indices.size() == 12 && mesh.sourceVertices == 8, "weld: equal vertices shared");
        // The key ignores the setup's constants (qwords 193..) and sees the vertices.
        std::vector<scene::BatchCopy> moved = {items[0]};
        moved[0].words[195 * 4] ^= 1;
        check(scene::meshKey(moved) == scene::meshKey({items[0]}), "key: the transform is not geometry");
        moved[0].words[3 * 4] ^= 1;
        check(!(scene::meshKey(moved) == scene::meshKey({items[0]})), "key: a vertex is");
        scene::MeshCache cache(2);
        check(cache.request({items[0]}) == 0, "cache: pending the first time");
        cache.wait();
        const scene::MeshId id = cache.request({items[0]});
        check(id != 0 && cache.mesh(id) && cache.mesh(id)->vertices.size() == 4, "cache: decoded on a worker, then found");
    }

    // The captures.
    uint32_t replayed = 0;
    for (int a = 1; a < argc; ++a) {
        std::string error;
        auto source = ps2::ReplaySource::open(argv[a], error);
        if (!source) {
            std::printf("mesh_cache: %s: %s (skipped)\n", argv[a], error.c_str());
            continue;
        }
        scene::MeshCache cache;
        Target target(cache);
        ps2::GraphicsStream stream(target);
        uint32_t frames = 0;
        while (source->nextFrame([&](const Gt4LinkRecord& h, const uint8_t* payload) { stream.record(h, payload); })) ++frames;
        cache.wait();
        const scene::MeshCache::Stats s = cache.stats();
        std::printf("mesh_cache: %s: %u frames, %llu displays, %llu batches, %llu vertices checked, %llu off by more than 1/16 px "
                    "(worst %llu/16), %llu strip starts without ADC; triangles %llu mesh / %llu CPU\n",
                    argv[a], frames, static_cast<unsigned long long>(target.displays), static_cast<unsigned long long>(target.batches),
                    static_cast<unsigned long long>(target.verticesChecked), static_cast<unsigned long long>(target.verticesOff),
                    static_cast<unsigned long long>(target.worstUnits), static_cast<unsigned long long>(target.startsWithoutAdc),
                    static_cast<unsigned long long>(target.trianglesMesh), static_cast<unsigned long long>(target.trianglesCpu));
        std::printf("mesh_cache: %llu objects asked for, %llu meshes decoded (%llu hits): %llu vertices welded from %llu, %llu triangles, "
                    "%.1f MiB; decode %.1f ms on the workers (%.3f ms a mesh)\n",
                    static_cast<unsigned long long>(target.objects), static_cast<unsigned long long>(s.decoded),
                    static_cast<unsigned long long>(s.hits), static_cast<unsigned long long>(s.vertices),
                    static_cast<unsigned long long>(s.sourceVertices), static_cast<unsigned long long>(s.triangles), s.bytes / 1048576.0,
                    s.decodeMilliseconds, s.decoded ? s.decodeMilliseconds / s.decoded : 0.0);
        if (target.batches == 0) {
            std::printf("mesh_cache: %s has no 3D batches (skipped)\n", argv[a]);
            continue;
        }
        // Steady state: what the last 50 frames (or all, when fewer) decoded per frame.
        const size_t last = std::min<size_t>(50, target.frames.size());
        uint64_t lateMeshes = 0, lateBytes = 0;
        for (size_t f = target.frames.size() - last; f < target.frames.size(); ++f)
            lateMeshes += target.frames[f].decoded, lateBytes += target.frames[f].bytes;
        std::printf("mesh_cache: steady state over the last %zu frames: %.2f new meshes and %.1f KiB a frame; %.1f MiB held at the "
                    "end, %llu evicted, %llu requests with per-frame geometry (not cached)\n",
                    last, last ? double(lateMeshes) / last : 0.0, last ? lateBytes / 1024.0 / last : 0.0, s.bytes / 1048576.0,
                    static_cast<unsigned long long>(s.evicted), static_cast<unsigned long long>(s.perFrame));
        std::printf("mesh_cache: ST %llu checked, %llu off; colours %llu checked, %llu off\n",
                    static_cast<unsigned long long>(target.stChecked), static_cast<unsigned long long>(target.stOff),
                    static_cast<unsigned long long>(target.coloursChecked), static_cast<unsigned long long>(target.coloursOff));
        uint64_t windingOff = 0;
        for (int r = 1; r <= 2; ++r) {
            const auto& f = target.facing[r];
            const int kept = f[0][0] >= f[0][1] ? 0 : 1;   // the sign the kept triangles share
            const uint64_t wrong = f[0][1 - kept] + f[1][kept];
            windingOff += wrong;
            std::printf("mesh_cache: renderer %d winding: kept %llu/%llu (+/-), culled %llu/%llu (+/-): %llu against the CPU's culling\n", r,
                        static_cast<unsigned long long>(f[0][0]), static_cast<unsigned long long>(f[0][1]),
                        static_cast<unsigned long long>(f[1][0]), static_cast<unsigned long long>(f[1][1]),
                        static_cast<unsigned long long>(wrong));
        }
        check(target.stOff == 0, std::string(argv[a]) + ": texture coordinates within one 4.12 unit");
        check(target.coloursOff == 0, std::string(argv[a]) + ": unlit colours exact");
        check(windingOff == 0, std::string(argv[a]) + ": the mesh's winding agrees with the CPU renderers' culling");
        ++replayed;
        check(target.verticesOff == 0, std::string(argv[a]) + ": every vertex within 1/16 pixel of the CPU renderers'");
        check(target.startsWithoutAdc == 0, std::string(argv[a]) + ": the strips' first vertices are the CPU renderers' ADC ones");
        check(target.trianglesMesh == target.trianglesCpu, std::string(argv[a]) + ": the same triangles");
        check(target.batchesOff == 0, std::string(argv[a]) + ": every batch matches");
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    if (!replayed) {
        std::puts("mesh_cache: unit checks passed; no capture with 3D batches (skipped)");
        return 77;
    }
    std::puts("mesh_cache: all checks passed");
    return 0;
}
