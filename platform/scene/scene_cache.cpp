// The scene cache (scene_cache.h).
#include "scene_cache.h"

#include "lit_road.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

namespace gt4::scene {

double threadCpuMicroseconds() {
#ifdef _WIN32
    // (preempted during the spin, the rate comes out low and the times high: on the safe side)
    static const double cyclesPerMicrosecond = [] {
        ULONG64 c0 = 0, c1 = 0;
        QueryThreadCycleTime(GetCurrentThread(), &c0);
        const auto t0 = std::chrono::steady_clock::now();
        volatile uint64_t spin = 0;
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(20)) spin = spin + 1;
        QueryThreadCycleTime(GetCurrentThread(), &c1);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        return us > 0.0 && c1 > c0 ? double(c1 - c0) / us : 1000.0;
    }();
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return double(cycles) / cyclesPerMicrosecond;
#else
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return double(ts.tv_sec) * 1e6 + double(ts.tv_nsec) / 1e3;
#endif
}

uint32_t materialState(const ItemSource& item) {
    // The PRIM's shading bits (Gouraud, textured, fog, blend, antialiased, texel coordinates), not its type or context;
    // then the context's fields that change the pixels and are not addresses (targets, scissors, the texture's place,
    // the mipmap levels' bases are where, not what).
    const uint32_t prim = item.prim & 0x1F8u;
    uint8_t fields[40] = {};
    size_t n = 0;
    auto put = [&](uint32_t v) { fields[n++] = static_cast<uint8_t>(v); };
    put(prim);
    put(prim >> 8);
    if (const gfx::Context* c = item.context) {
        put(c->blendA); put(c->blendB); put(c->blendC); put(c->blendD); put(c->blendFixed);
        put(c->alphaTest); put(c->alphaFunc); put(c->alphaRef); put(c->alphaFail);
        put(c->destTest); put(c->destPass);
        put(c->depthTest); put(c->depthFunc); put(c->depthWrite);
        put(c->forceAlpha);
        put(c->colorKeep); put(c->colorKeep >> 8); put(c->colorKeep >> 16); put(c->colorKeep >> 24);
        put(c->texture.useAlpha); put(c->texture.function);
        const gfx::Sampler& s = c->sampler;
        put(s.wrapU); put(s.wrapV); put(s.lodFixed); put(s.mipMax); put(s.magLinear); put(s.minFilter); put(s.lodL);
        put(static_cast<uint16_t>(s.lodK)); put(static_cast<uint16_t>(s.lodK) >> 8);
    }
    put(item.texAlpha0); put(item.texAlpha1); put(item.texBlackTransparent); put(item.perPixelBlend);
    return static_cast<uint32_t>(XXH3_64bits(fields, n));
}

MaterialKey materialKey(const ItemSource& item) {
    MaterialKey k;
    const gfx::ModelJob& job = *item.batch.job;
    k.texture = item.texture;
    k.palette = item.palette;
    k.body = job.chain >> 16;
    k.dot = job.dot;
    k.state = materialState(item);
    // colours: 0 (CONTRACTS.md a.8, 51a14a9): the batch's colour rows (qwords 204..216, 0049E228's) are per-frame lighting
    // constants, not the material's identity; B7 reads them per frame from the batch.
    k.colours = 0;
    return k;
}

SceneCache::SceneCache(unsigned meshWorkers) : meshCache(meshWorkers) {}

InstanceIds SceneCache::assign(const InstanceData& instance, const ItemSource* items, size_t count, std::vector<MaterialId>& materials,
                               SceneCounters& counters) {
    InstanceIds ids;
    if (!count) return ids;
    // The materials, one per item. Items from the same draw (the same state and texture) with the same lighting body and
    // dot model have the same key: one lookup for each run of them (B6's PERF: the table's lock once, not per item).
    ids.firstMaterial = static_cast<uint32_t>(materials.size());
    ids.materialCount = static_cast<uint32_t>(count);
    const ItemSource* last = nullptr;
    MaterialId lastId = 0;
    for (size_t i = 0; i < count; ++i) {
        const ItemSource& item = items[i];
        const gfx::ModelJob& job = *item.batch.job;
        if (last && last->context == item.context && last->prim == item.prim && last->texture == item.texture &&
            last->palette == item.palette && last->texAlpha0 == item.texAlpha0 && last->texAlpha1 == item.texAlpha1 &&
            last->texBlackTransparent == item.texBlackTransparent && last->perPixelBlend == item.perPixelBlend &&
            (last->batch.job->chain >> 16) == (job.chain >> 16) && last->batch.job->dot == job.dot) {
            materials.push_back(lastId);
            continue;
        }
        bool made = false;
        lastId = classifier ? materialTable.id(materialKey(item), [&] { return classifier(item); }, &made)
                            : materialTable.id(materialKey(item), {}, &made);
        counters.materialsNew += made;
        materials.push_back(lastId);
        last = &item;
    }
    // The mesh: none for geometry that is this frame's only: skinned, and the billboards' quads, built facing the camera
    // every frame (A7 step 1b): no key hashed, no decode queued (their materials above still feed the classifier, F2b).
    if (instance.flags & gfx::scene::kInstSkinned) {
        ++counters.meshesSkinned;
        return ids;
    }
    if (instance.flags & gfx::scene::kInstBillboard) {
        ++counters.meshesPerFrame;
        return ids;
    }
    // Out of the binding and never substituted (CONTRACTS a.5.7): no mesh to decode for them. Only while they stay out:
    // the reflection's meshes are static course models, so if B9 makes the reflection a kViewReflection view, its skip goes.
    if (instance.flags & (gfx::scene::kInstLightMap | gfx::scene::kInstReflection)) {
        ++counters.meshesOutOfBinding;
        return ids;
    }
    refs.resize(count);
    for (size_t i = 0; i < count; ++i) refs[i] = items[i].batch;
    MeshRequest outcome = MeshRequest::Ready;
    ids.mesh = meshCache.request(refs.data(), refs.size(), &outcome);
    switch (outcome) {
        case MeshRequest::Queued: ++counters.meshesNew; break;
        case MeshRequest::Pending: ++counters.meshesPending; break;
        case MeshRequest::PerFrame: ++counters.meshesPerFrame; break;
        default: break;
    }
    if (trackObjects && ids.mesh && instance.object) {   // (the acceptance's check: only when its log is on)
        const uint64_t hash = XXH3_64bits(materials.data() + ids.firstMaterial, size_t(ids.materialCount) * sizeof(MaterialId));
        // (by object and view: some objects' batches differ per view, e.g. a Cmd the game rewrites for each view it draws
        // into, so each view keeps its own mesh, all of them cached)
        auto [it, fresh] = seen.try_emplace(instance.object ^ (uint64_t(instance.view) << 56), Seen{ids.mesh, hash});
        if (!fresh) {
            if (it->second.mesh != ids.mesh) ++objectCounts.objectsMeshChanged;
            else if (it->second.materials != hash) ++objectCounts.objectsMaterialsChanged;
            else ++objectCounts.objectsSame;
            it->second = Seen{ids.mesh, hash};
        }
    }
    return ids;
}

void SceneCache::endFrame() {
    meshCache.endFrame();
    materialTable.endFrame();
    ++frames;
    if (frames % kLitRoadFrames == 0)   // (N3's verdicts unseen for kLitRoadFrames frames: another course's, dropped)
        for (auto it = litRoadVerdicts.begin(); it != litRoadVerdicts.end();) {
            if (frames - it->second.lastFrame < kLitRoadFrames) {
                ++it;
                continue;
            }
            for (int k = 0; k < 4; ++k) roadBase[k] -= it->second.base[k];
            it = litRoadVerdicts.erase(it);
        }
}

bool SceneCache::roadBaseAlbedo(float (&rgb)[3]) const {
    if (!(roadBase[3] > 1e-3)) return false;
    for (int k = 0; k < 3; ++k) rgb[k] = float(std::clamp(roadBase[k] / roadBase[3], 0.0, 1.0));
    return true;
}

namespace {

// The rotation part of objectToWorld, each row made unit (a scale is not a rotation).
void rotationOf(const float (&m)[4][4], float (&r)[3][3]) {
    for (int i = 0; i < 3; ++i) {
        const float s = std::sqrt(m[i][0] * m[i][0] + m[i][1] * m[i][1] + m[i][2] * m[i][2]);
        for (int q = 0; q < 3; ++q) r[i][q] = s > 0.0f ? m[i][q] / s : 0.0f;
    }
}

}  // namespace

void SceneCache::litRoad(const InstanceData& instance, const ItemSource* items, size_t count, const MaterialId* materials,
                         const std::function<bool(uint64_t, TextureSummary&)>& summaries, uint8_t* out,
                         SceneCounters& counters) {
    std::fill(out, out + count, uint8_t(0));
    // The cheap tests first, per material and remembered: the layer's draw state, and a texture that is no glow.
    litRoadCandidate.assign(count, 0);
    bool any = false;
    for (size_t i = 0; i < count; ++i) {
        auto [it, fresh] = litRoadMaterials.try_emplace(materials[i], uint8_t(1));
        if (fresh) {
            TextureSummary ts;
            const bool known = summaries && summaries(items[i].texture, ts) && ts.known && ts.width && ts.height;
            it->second = litRoadState(items[i]) && known && ts.centreToRim < 3.0f ? 2 : 1;
            if (it->second == 1 && known && roadBaseState(items[i]))
                it->second = 3;   // (judged only while roadBaseOn)
        }
        litRoadCandidate[i] = it->second == 2 || (it->second == 3 && roadBaseOn) ? it->second : 0;
        any |= litRoadCandidate[i] != 0;
    }
    if (!any) return;
    // The geometry, per (material, mesh, item): judged once, through the instance it is first seen with.
    refs.resize(count);
    for (size_t i = 0; i < count; ++i) refs[i] = items[i].batch;
    const MeshKey mesh = meshKey(refs.data(), refs.size());
    float rotation[3][3];
    rotationOf(instance.objectToWorld.m, rotation);
    for (size_t i = 0; i < count; ++i) {
        if (!litRoadCandidate[i]) continue;
        const LitRoadKey key{materials[i], mesh, uint32_t(i)};
        auto it = litRoadVerdicts.find(key);
        if (litRoadCandidate[i] == 3) {   // the stand-in's road base: its share once per key, never a skip
            if (it == litRoadVerdicts.end()) {
                LitRoadEntry e;
                TextureSummary ts;
                const bool known = summaries && summaries(items[i].texture, ts) && ts.known;
                const DecodedBatch batch = decodeBatch(*items[i].batch.job, items[i].batch.words);
                const LitRoadMeasure measure = measureLitRoad(batch, instance.objectToWorld.m, known ? &ts : nullptr);
                if (known && roadBaseVerdict(measure)) {
                    for (int k = 0; k < 3; ++k) e.base[k] = float(std::pow(ts.mean[k] / 255.0, 2.2) * measure.upArea);
                    e.base[3] = measure.upArea;
                    for (int k = 0; k < 4; ++k) roadBase[k] += e.base[k];
                }
                it = litRoadVerdicts.emplace(key, e).first;
            }
            it->second.lastFrame = frames;
            continue;
        }
        if (it == litRoadVerdicts.end()) {
            LitRoadEntry e;
            TextureSummary ts;
            const bool known = summaries && summaries(items[i].texture, ts);
            const DecodedBatch batch = decodeBatch(*items[i].batch.job, items[i].batch.words);
            const LitRoadMeasure measure = measureLitRoad(batch, instance.objectToWorld.m, known ? &ts : nullptr);
            e.skip = litRoadVerdict(measure) ? 1 : 0;
            // GT4_N3_LOG=1 (N3's acceptance): each new verdict with its measures, once per key
            static const bool log = [] { const char* v = std::getenv("GT4_N3_LOG"); return v && *v == '1'; }();
            if (log)
                std::printf("n3 verdict: texture %016llx material %u item %u vc %.1f a %.1f c/r %.2f share %.3f width %.3f "
                            "texel %.4f -> %s\n", static_cast<unsigned long long>(items[i].texture), materials[i], unsigned(i),
                            measure.vertexLuminance, measure.vertexAlpha, measure.centreToRim, measure.upShare, measure.width,
                            measure.texel, e.skip ? "skip" : "keep");
            std::memcpy(e.rotation, rotation, sizeof rotation);
            it = litRoadVerdicts.emplace(key, e).first;
            ++counters.n3Judged;
        }
        LitRoadEntry& e = it->second;
        e.lastFrame = frames;
        uint8_t skip = e.skip;
        if (skip) {
            float worst = 0.0f;
            for (int r = 0; r < 3; ++r)
                for (int q = 0; q < 3; ++q) worst = std::max(worst, std::fabs(rotation[r][q] - e.rotation[r][q]));
            if (worst > 1e-3f) {   // another rotation than its verdict's: kept, counted
                skip = 0;
                ++counters.n3LaterFail;
            }
        }
        counters.n3Skipped += skip;
        out[i] = skip;
    }
}

size_t SceneCache::litRoadCandidates(const MaterialId* materials, size_t count) const {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto it = litRoadMaterials.find(materials[i]);
        n += it != litRoadMaterials.end() && it->second == 2;
    }
    return n;
}

}  // namespace gt4::scene
