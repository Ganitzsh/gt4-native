// The scene cache (scene_cache.h).
#include "scene_cache.h"

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

namespace gt4::scene {

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
    // The materials, one per item.
    ids.firstMaterial = static_cast<uint32_t>(materials.size());
    ids.materialCount = static_cast<uint32_t>(count);
    const uint64_t created = materialTable.stats().created;
    for (size_t i = 0; i < count; ++i) {
        const ItemSource& item = items[i];
        materials.push_back(classifier ? materialTable.id(materialKey(item), [&] { return classifier(item); })
                                       : materialTable.id(materialKey(item)));
    }
    counters.materialsNew += materialTable.stats().created - created;
    // The mesh: none for geometry that is this frame's only.
    if (instance.flags & gfx::scene::kInstSkinned) {
        ++counters.meshesSkinned;
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
    if (ids.mesh && instance.object) {
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
}

}  // namespace gt4::scene
