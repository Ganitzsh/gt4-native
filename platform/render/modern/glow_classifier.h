#pragma once
// F2b's glow class (PLAN.md F2b): a material is a lamp-head glow when its draw state has the street-lamp draw's
// signature and its texture looks like a glow. B6's scene cache calls classify() for each material key it sees for the
// first time (SceneCache::setClassifier), on the front-end thread, under the material table's lock; the bits are kept
// with the material's id. The glow's colour (the texture's mean) is kept here by texture identity, since the id does
// not exist yet when the classifier runs; the render thread reads it (glowColour) when it first meets the id.
//
// Lock order: the material table's lock, then this table's, never back into the material table. The render thread
// takes only this table's lock.
#include "scene_cache.h"       // scene::ItemSource
#include "texture_summary.h"   // scene::TextureSummary

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace gt4::render {

inline constexpr uint32_t kMaterialGlow = 1u << 0;   // MaterialTable::classes bit: a lamp-head glow (F2b)

// The street-lamp draw's state (F2b step 0, R11's draw 309): textured, blended (Cs - Cd) As + Cd, alpha test EQUAL 0x80
// (the glow's core writes Z), Z written, the batch unlit (lighting body 0).
bool glowSignature(const scene::ItemSource& item);

// The texture check: bright in the centre against the rim, with an opaque core (alpha 0x80) for the alpha test to pass.
struct GlowScores {
    float centreToRim = 0.0f;
    float alphaCore = 0.0f;   // 0..1
    bool glow = false;
};
GlowScores glowTexture(const scene::TextureSummary& summary);

class GlowClassifier {
public:
    // The class bits for a new material: kMaterialGlow when the signature and the texture agree. `summary` is the
    // texture's (the front end's textureSummary), null when it has none (then no glow). Logs each texture's verdict once
    // (to `log`, when set) with its size and scores.
    uint32_t classify(const scene::ItemSource& item, const scene::TextureSummary* summary);
    // The glow's colour (0..1 RGB) by the material's texture identity; false when the texture was not judged a glow.
    bool glowColour(uint64_t texture, float out[3]) const;
    void setLog(std::FILE* f) { log = f; }

    struct Stats {
        uint64_t judged = 0, signature = 0, glows = 0, noSummary = 0;
    };
    Stats stats() const;

private:
    struct Entry {
        float colour[3] = {};
        bool glow = false;
    };
    mutable std::mutex lock;
    std::unordered_map<uint64_t, Entry> byTexture;
    Stats counters;
    std::FILE* log = nullptr;
};

}  // namespace gt4::render
