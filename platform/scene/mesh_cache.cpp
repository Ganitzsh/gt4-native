// The mesh cache (mesh_cache.h). The batch layout and the decompression rules are the 3D renderers'
// (platform/gs/renderers3d.cpp, docs/VU1_PROGRAMS.md): qwords 0..63 positions, 64..127 texture coordinates (their
// w words: the strips), 128..191 colours or normals, 192 the GIF tag, 242 .w the key-frame weight.
#include "mesh_cache.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

namespace gt4::scene {
namespace {

constexpr uint32_t kPositions = 0, kSt = 64, kColors = 128, kTag = 192, kWeight = 242;
// PRIM's FGE (bit 5), ABE (6), AA1 (7) and CTXT (9); in the GIF tag's second word they are bits 20, 21, 22 and 24 (PRIM at
// the tag's bits 47..57)
constexpr uint32_t kPrimNotGeometry = 0x2E0u, kTagPrimNotGeometry = 0x01700000u;
constexpr float kNormalScale = 1.0f / 127.0f;

// The VU's reading of a float word: no infinities, NaNs or denormals (renderers3d.cpp vuFloat).
float vuFloat(uint32_t bits) {
    if (!(bits & 0x7F800000u)) bits &= 0x80000000u;
    if ((bits & 0x7F800000u) == 0x7F800000u) bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    return std::bit_cast<float>(bits);
}
float saturate(float f) {
    if (f != f) return 0.0f;
    return std::clamp(f, -3.402823466e38f, 3.402823466e38f);
}
float itof0(uint32_t w) { return static_cast<float>(static_cast<int32_t>(w)); }
float itof12(uint32_t w) { return static_cast<float>(static_cast<int32_t>(w)) / 4096.0f; }

const uint32_t* qword(const uint32_t* words, uint32_t q) { return words + size_t(q % 256) * 4; }

uint8_t channel(uint32_t word) {   // the low byte, as the GS takes it (renderers3d.cpp gsVertex)
    return static_cast<uint8_t>(word & 0xFF);
}
uint32_t packColour(const uint32_t* w) { return channel(w[0]) | channel(w[1]) << 8 | channel(w[2]) << 16 | uint32_t(channel(w[3])) << 24; }

// The lighting chain's routine and body (ModelJob::chain): 5 = no decompression.
uint32_t unpackOf(const gfx::ModelJob& job) { return (job.chain & 1) ? (job.chain >> 4) & 0xF : 5; }
uint32_t bodyOf(const gfx::ModelJob& job) { return (job.chain & 1) ? job.chain >> 16 : 0; }

}  // namespace

DecodedBatch decodeBatch(const gfx::ModelJob& job, const uint32_t* words) {
    DecodedBatch out;
    // The strips: qword 64 .w their count, qwords 65.. .w each length as output qwords (3 a vertex) from its first
    // to its second-to-last vertex.
    const uint32_t strips = qword(words, kSt)[3] & 0xFFFF;
    uint32_t total = 0;
    std::vector<uint32_t> lengths;
    for (uint32_t k = 0; k < strips && total <= 256; ++k) {
        const uint32_t n = (qword(words, kSt + 1 + k)[3] & 0xFFFF) / 3u + 2;
        lengths.push_back(n);
        total += n;
    }
    const uint32_t count = std::min<uint32_t>({job.count, total, 256});
    const uint32_t n = std::min<uint32_t>(job.nloop, 256);   // the vertices the chain decodes
    const uint32_t unpack = unpackOf(job), body = bodyOf(job);
    const bool lit = (job.chain & 1) != 0;
    out.texture = (job.prim & 0x10) != 0;   // TME
    out.animated = unpack == 3 || unpack == 4;
    out.normals = lit && body != 0x0B0;
    const float t = vuFloat(qword(words, kWeight)[3]), rest = 1.0f - t;
    // The raw words' flags, as gsn_models.comp runs the chain: the routine decodes k < NLOOP, the body lights those
    // (the fade an even number: one more when NLOOP is odd), and the N.L model's quirk reads the next vertex's normal.
    const uint32_t litCount = !lit ? 0 : body == 0x16F8 ? std::min<uint32_t>((job.nloop + 1) & ~1u, 257) : job.nloop;
    const bool modelBody = body != 0x0B0 && body != 0xCA8 && body != 0xEA0 && body != 0x10A8 && body != 0x12C8 && body != 0x1608 &&
                           body != 0x1508 && body != 0x16F8;   // the N.L model (gt4_lighting.glsl lightVertex's last case)
    const bool nextNormal = lit && modelBody && (job.dot & kDotNextNormal) != 0;
    out.vertices.resize(count);
    out.raw.resize(count);
    out.starts.assign(count, 0);
    for (uint32_t k = 0; k < count; ++k) {
        MeshVertex& v = out.vertices[k];
        const uint32_t* p = qword(words, kPositions + k);
        const uint32_t* st = qword(words, kSt + k);
        const uint32_t* c = qword(words, kColors + k);
        MeshRawVertex& r = out.raw[k];
        std::memcpy(r.position, p, sizeof r.position);
        std::memcpy(r.st, st, sizeof r.st);
        std::memcpy(r.colour, c, sizeof r.colour);
        r.flags = (k < litCount ? kRawLit : 0) | (lit && k < job.nloop ? kRawDecoded : 0);
        // (the strip's index and the two vertices before it: set below, once the strips are walked)
        if (nextNormal && k < litCount) {   // (else 0: welding is not split by a value no one reads)
            std::memcpy(r.next, qword(words, kColors + k + 1), sizeof r.next);
            if (k + 1 < job.nloop) r.flags |= kRawNextDecoded;
        }
        // As sent (the chain's routine leaves the vertices past NLOOP, and all of them when it has none).
        for (int i = 0; i < 3; ++i) v.position[i] = vuFloat(p[i]);
        v.st[0] = vuFloat(st[0]);
        v.st[1] = vuFloat(st[1]);
        v.colour = packColour(c);
        if (lit && k < n) {
            switch (unpack) {
            case 0:   // 0x1908: positions as integers, normals in the ST slot (bytes over 127)
                for (int i = 0; i < 3; ++i) v.position[i] = itof0(p[i]), v.normal[i] = itof0(st[i]) * kNormalScale;
                v.st[0] = v.st[1] = 0.0f;
                break;
            case 1:   // 0x1878: positions, ST in 4.12, normals in the colour slot
                for (int i = 0; i < 3; ++i) v.position[i] = itof0(p[i]), v.normal[i] = itof0(c[i]) * kNormalScale;
                v.st[0] = itof12(st[0]);
                v.st[1] = itof12(st[1]);
                v.colour = 0;
                break;
            case 2:   // 0x1810: positions, ST
                for (int i = 0; i < 3; ++i) v.position[i] = itof0(p[i]);
                v.st[0] = itof12(st[0]);
                v.st[1] = itof12(st[1]);
                break;
            case 3:   // 0x1990 / 0x1A38: two key frames 32 qwords apart, blended by qword 242 .w
            case 4: {
                const uint32_t* pb = qword(words, kPositions + 32 + k);
                const uint32_t* sb = qword(words, kSt + 32 + k);
                const uint32_t* cb = qword(words, kColors + 32 + k);
                for (int i = 0; i < 3; ++i) v.position[i] = saturate(vuFloat(p[i]) * rest + vuFloat(pb[i]) * t);
                for (int i = 0; i < 2; ++i) v.st[i] = saturate(vuFloat(st[i]) * rest + vuFloat(sb[i]) * t);
                if (unpack == 4) {   // integer colours
                    uint32_t blended[4];
                    for (int i = 0; i < 4; ++i) {
                        const float f = itof0(c[i]) * rest + itof0(cb[i]) * t;
                        blended[i] = static_cast<uint32_t>(static_cast<int32_t>(std::clamp(f, -2147483648.0f, 2147483520.0f)));
                    }
                    v.colour = packColour(blended);
                } else {             // floats: normals
                    for (int i = 0; i < 3; ++i) v.normal[i] = saturate(vuFloat(c[i]) * rest + vuFloat(cb[i]) * t);
                    v.colour = 0;
                }
                break;
            }
            default:  // none: the colour slot holds normals as floats
                for (int i = 0; i < 3; ++i) v.normal[i] = vuFloat(c[i]);
                break;
            }
        }
    }
    // Triangles: vertex j >= 2 of a strip makes (j - 2, j - 1, j); every other one turns the other way.
    uint32_t first = 0;
    for (const uint32_t length : lengths) {
        for (uint32_t j = 0; j < length && first + j < count; ++j) {
            const uint32_t v = first + j;
            MeshRawVertex& r = out.raw[v];
            r.flags |= (j < 2 ? kRawStart : 0) | (j & 1 ? kRawOdd : 0);
            if (j >= 2) {
                for (uint32_t b = 0; b < 2; ++b) {
                    std::memcpy(r.before[b], qword(words, kPositions + v - 1 - b), sizeof r.before[b]);
                    if (lit && v - 1 - b < job.nloop) r.flags |= b ? kRawBefore2Decoded : kRawBefore1Decoded;
                }
            }
            if (j < 2) {
                out.starts[v] = 1;
                continue;
            }
            if ((j - 2) & 1) out.triangles.insert(out.triangles.end(), {v - 1, v - 2, v});
            else out.triangles.insert(out.triangles.end(), {v - 2, v - 1, v});
            out.turned.push_back(static_cast<uint8_t>((j - 2) & 1));
        }
        first += length;
    }
    return out;
}

namespace {
struct WeldKey {   // a vertex decoded and as sent: welded only when both are equal
    MeshVertex vertex;
    MeshRawVertex raw;
};
struct VertexHash {
    size_t operator()(const WeldKey& v) const { return static_cast<size_t>(XXH3_64bits(&v, sizeof v)); }
};
struct VertexEqual {
    bool operator()(const WeldKey& a, const WeldKey& b) const { return !std::memcmp(&a, &b, sizeof a); }
};
static_assert(sizeof(MeshVertex) == 36 && sizeof(MeshRawVertex) == 76 && sizeof(WeldKey) == 112,
              "no padding: vertices are hashed and compared as bytes");
}  // namespace

Mesh buildMesh(const std::vector<BatchCopy>& items) {
    Mesh mesh;
    mesh.key = meshKey(items);
    std::unordered_map<WeldKey, uint32_t, VertexHash, VertexEqual> welded;
    for (const BatchCopy& item : items) {
        const DecodedBatch batch = decodeBatch(item.job, item.words);
        std::vector<uint32_t> remap(batch.vertices.size());
        for (size_t k = 0; k < batch.vertices.size(); ++k) {
            auto [it, added] = welded.try_emplace(WeldKey{batch.vertices[k], batch.raw[k]}, static_cast<uint32_t>(mesh.vertices.size()));
            if (added) {
                mesh.vertices.push_back(batch.vertices[k]);
                mesh.raw.push_back(batch.raw[k]);
            }
            remap[k] = it->second;
        }
        MeshItem m;
        m.firstIndex = static_cast<uint32_t>(mesh.indices.size());
        for (const uint32_t i : batch.triangles) mesh.indices.push_back(remap[i]);
        mesh.turned.insert(mesh.turned.end(), batch.turned.begin(), batch.turned.end());
        m.indexCount = static_cast<uint32_t>(mesh.indices.size()) - m.firstIndex;
        m.renderer = item.job.renderer;
        m.chain = item.job.chain;
        m.dot = item.job.dot;
        m.prim = item.job.prim;
        m.normals = batch.normals;
        m.texture = batch.texture;
        m.animated = batch.animated;
        mesh.items.push_back(m);
        mesh.sourceVertices += static_cast<uint32_t>(batch.vertices.size());
    }
    return mesh;
}

MeshKey meshKey(const BatchRef* items, size_t count) {
    MeshKey key;
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (size_t i = 0; i < count; ++i) {
        const gfx::ModelJob& job = *items[i].job;
        // The vertex data, the strips and the tag (qwords 0..192); not the setup's constants, the lights or the
        // header, which change with the view, not with the geometry, nor a key-frame blend's weight, which changes
        // per frame (such items are not cached: MeshCache::request).
        // The tag and the job's PRIM without the bits that change the pixels but not the geometry (fog, blend, antialiasing,
        // context): GT4 draws the same object into its views with those set differently (B6: the main view and the mirrors
        // gave one object three meshes). The item's MeshItem::prim is the first decode's: the per-frame state is the draw's.
        // What decodeBatch reads: the strips (qword 64 .w, then one qword each from 65), and the first `count` vertices'
        // positions, ST and colours (B6: the key costs a third or less of hashing qwords 0..191, and words a batch leaves
        // from an earlier one no longer split it). A batch past its regions' 64 qwords is hashed whole, as before.
        const uint32_t* words = items[i].words;
        const uint32_t vertices = std::min<uint32_t>(job.count, 256);
        const uint32_t stripQwords = 1 + (words[kSt * 4 + 3] & 0xFFFF);
        if (vertices <= 64 && stripQwords <= 64) {
            XXH3_64bits_update(&state, &vertices, sizeof vertices);
            XXH3_64bits_update(&state, words + kPositions * 4, size_t(vertices) * 16);
            XXH3_64bits_update(&state, words + kSt * 4, size_t(std::max(vertices, stripQwords)) * 16);
            XXH3_64bits_update(&state, words + kColors * 4, size_t(vertices) * 16);
        } else {
            XXH3_64bits_update(&state, words, kTag * 16);
        }
        uint32_t tag[4];
        std::memcpy(tag, items[i].words + kTag * 4, sizeof tag);
        tag[1] &= ~kTagPrimNotGeometry;
        XXH3_64bits_update(&state, tag, sizeof tag);
        // The next-normal bit (the raw vertices keep the next normal only with it), and the normal it reads past the
        // last decoded vertex (qword 128 + NLOOP), which the words hashed above may not hold.
        const uint32_t fields[6] = {job.count, job.nloop, job.renderer, job.chain, job.prim & ~kPrimNotGeometry, job.dot & kDotNextNormal};
        XXH3_64bits_update(&state, fields, sizeof fields);
        if (job.dot & kDotNextNormal) XXH3_64bits_update(&state, words + ((kColors + job.nloop) % 256) * 4, 8);
    }
    key.packets = XXH3_64bits_digest(&state);
    if (count) {
        key.renderer = items[0].job->renderer;
        key.chain = items[0].job->chain;
        key.chainUnpack = unpackOf(*items[0].job);
    }
    return key;
}

static std::vector<BatchRef> refsOf(const std::vector<BatchCopy>& items) {
    std::vector<BatchRef> refs(items.size());
    for (size_t i = 0; i < items.size(); ++i) refs[i] = {&items[i].job, items[i].words};
    return refs;
}

MeshKey meshKey(const std::vector<BatchCopy>& items) {
    const std::vector<BatchRef> refs = refsOf(items);
    return meshKey(refs.data(), refs.size());
}

std::vector<std::pair<uint32_t, uint32_t>> groupByMatrix(const gfx::ModelJob* jobs, size_t count) {
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    for (uint32_t i = 0; i < count; ++i) {
        if (!runs.empty() && !std::memcmp(jobs[i].matrix, jobs[runs.back().first].matrix, sizeof jobs[i].matrix)) ++runs.back().second;
        else runs.push_back({i, 1});
    }
    return runs;
}

// --- The cache ----------------------------------------------------------------------------------------------

MeshCache::MeshCache(unsigned workers) {
    if (!workers) workers = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 4u);
    for (unsigned i = 0; i < workers; ++i) threads.emplace_back([this] { work(); });
}

MeshCache::~MeshCache() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
    }
    wake.notify_all();
    for (std::thread& t : threads) t.join();
}

MeshId MeshCache::request(const std::vector<BatchCopy>& items) {
    const std::vector<BatchRef> refs = refsOf(items);
    return request(refs.data(), refs.size());
}

MeshId MeshCache::request(const BatchRef* items, size_t count, MeshRequest* outcome) {
    MeshRequest found;
    if (!outcome) outcome = &found;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t unpack = unpackOf(*items[i].job);
        if (unpack == 3 || unpack == 4) {   // a key-frame blend: its geometry is this frame's only
            std::lock_guard<std::mutex> guard(lock);
            ++counters.requests;
            ++counters.perFrame;
            *outcome = MeshRequest::PerFrame;
            return 0;
        }
    }
    const MeshKey key = meshKey(items, count);
    std::unique_lock<std::mutex> guard(lock);
    ++counters.requests;
    auto it = ids.find(key);
    if (it != ids.end()) {
        ++counters.hits;
        Entry& e = *entries[it->second - 1];
        e.lastUsed = frame;
        const bool ready = e.ready.load(std::memory_order_acquire);
        *outcome = ready ? MeshRequest::Ready : MeshRequest::Pending;
        return ready ? it->second : 0;
    }
    entries.push_back(std::make_unique<Entry>());
    const MeshId id = static_cast<MeshId>(entries.size());
    entries.back()->lastUsed = frame;
    ids.emplace(key, id);
    std::vector<BatchCopy> copies(count);   // (only on a miss: the worker decodes from them after this list goes)
    for (size_t i = 0; i < count; ++i) {
        copies[i].job = *items[i].job;
        std::memcpy(copies[i].words, items[i].words, sizeof copies[i].words);
    }
    queue.emplace_back(id, std::move(copies));
    ++counters.pending;
    guard.unlock();
    wake.notify_one();
    *outcome = MeshRequest::Queued;
    return 0;
}

MeshId MeshCache::find(const MeshKey& key) const {
    std::lock_guard<std::mutex> guard(lock);
    auto it = ids.find(key);
    if (it == ids.end()) return 0;
    return entries[it->second - 1]->ready.load(std::memory_order_acquire) ? it->second : 0;
}

std::shared_ptr<const Mesh> MeshCache::mesh(MeshId id) const {
    std::lock_guard<std::mutex> guard(lock);
    if (id == 0 || id > entries.size()) return nullptr;
    const Entry& e = *entries[id - 1];
    return e.ready.load(std::memory_order_acquire) && !e.evicted ? e.mesh : nullptr;
}

void MeshCache::setLimits(uint32_t frames, uint64_t bytes) {
    std::lock_guard<std::mutex> guard(lock);
    unusedFrames = frames;
    budgetBytes = bytes;
}

void MeshCache::endFrame() {
    std::lock_guard<std::mutex> guard(lock);
    ++frame;
    auto evict = [&](Entry& e) {
        counters.bytes -= e.mesh->bytes();
        ids.erase(e.mesh->key);   // asked for again: decoded again, under a new id
        e.mesh.reset();          // (a reader holding it keeps it)
        e.evicted = true;
        ++counters.evicted;
    };
    std::vector<Entry*> held;
    for (auto& entry : entries) {
        Entry& e = *entry;
        if (!e.ready.load(std::memory_order_acquire) || e.evicted) continue;   // (pending ones are the workers')
        if (frame - e.lastUsed > unusedFrames) evict(e);
        else held.push_back(&e);
    }
    if (counters.bytes <= budgetBytes) return;
    std::sort(held.begin(), held.end(), [](const Entry* a, const Entry* b) { return a->lastUsed < b->lastUsed; });
    for (Entry* e : held) {
        // never what the last 8 frames used: lists still in flight name them (CONTRACTS a.8, F2b: a render-thread lookup
        // of a dropped id finds nothing and is counted, but within 8 frames it never happens; the lists sorted oldest first)
        if (counters.bytes <= budgetBytes || frame - e->lastUsed < 8) break;
        evict(*e);
    }
}

void MeshCache::wait() {
    std::unique_lock<std::mutex> guard(lock);
    idle.wait(guard, [this] { return queue.empty() && busy == 0; });
}

MeshCache::Stats MeshCache::stats() const {
    std::lock_guard<std::mutex> guard(lock);
    return counters;
}

void MeshCache::work() {
    for (;;) {
        std::unique_lock<std::mutex> guard(lock);
        wake.wait(guard, [this] { return stopping || !queue.empty(); });
        if (stopping) return;
        auto [id, items] = std::move(queue.front());
        queue.pop_front();
        Entry* entry = entries[id - 1].get();
        ++busy;
        guard.unlock();
        const auto started = std::chrono::steady_clock::now();
        auto built = std::make_shared<const Mesh>(buildMesh(items));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        guard.lock();   // (published under the lock, with its bytes counted: endFrame never sees it half done)
        entry->mesh = std::move(built);
        entry->ready.store(true, std::memory_order_release);
        --busy;
        --counters.pending;
        ++counters.decoded;
        counters.vertices += entry->mesh->vertices.size();
        counters.sourceVertices += entry->mesh->sourceVertices;
        counters.triangles += entry->mesh->indices.size() / 3;
        counters.bytes += entry->mesh->bytes();
        counters.decodedBytes += entry->mesh->bytes();
        counters.decodeMilliseconds += ms;
        if (queue.empty() && busy == 0) idle.notify_all();
    }
}

}  // namespace gt4::scene
