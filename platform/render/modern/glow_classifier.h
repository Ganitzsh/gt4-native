#pragma once
// F2b's glow class (PLAN.md F2b): a material is a lamp-head glow when its draw state has the street-lamp draw's
// signature and its texture looks like a glow. B6's scene cache calls classify() for each material key it sees for the
// first time (SceneCache::setClassifier), on the front-end thread, under the material table's lock; the bits are kept
// with the material's id and never change for it. The glow's colour (the texture's mean, 8 bits a channel) travels in
// the same bits, so the render thread reads everything from MaterialTable::classes and nothing else.
//
// Lock order: the material table's lock, then this classifier's, never back into the material table. Only the
// front-end thread calls classify(); the render thread never touches the classifier.
#include "scene_cache.h"       // scene::ItemSource
#include "texture_summary.h"   // scene::TextureSummary

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace gt4::render {

// MaterialTable::classes bits: bit 0 a lamp-head glow (F2b), bits 8..31 its colour (R, G, B, 8 bits each). The word
// holds data as well as flags: bits 8..31 belong to the glow class, any other class takes a bit in 1..7, and readers
// test (classes & kMaterialGlow), never classes != 0.
inline constexpr uint32_t kMaterialGlow = 1u << 0;
inline uint32_t glowClasses(const uint8_t mean[3]) {
    return kMaterialGlow | (uint32_t(mean[0]) << 8) | (uint32_t(mean[1]) << 16) | (uint32_t(mean[2]) << 24);
}
inline void glowColour(uint32_t classes, float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = static_cast<float>((classes >> (8 + 8 * c)) & 0xFF) / 255.0f;
}

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
    // The class bits for a new material: glowClasses(the texture's mean) when the signature and the texture agree, else
    // 0. `summary` is the texture's (the front end's textureSummary), null when it has none (then no glow). Logs each
    // texture's verdict once (to `log`, when set) with its size and scores.
    uint32_t classify(const scene::ItemSource& item, const scene::TextureSummary* summary);
    void setLog(std::FILE* f) { log = f; }

    struct Stats {
        uint64_t judged = 0, signature = 0, glows = 0, noSummary = 0, textures = 0;
    };
    Stats stats() const;

private:
    mutable std::mutex lock;
    // A texture's verdict (its class bits), judged once by identity: a content that changes under the same identity
    // keeps its first verdict and colour (the identity is meant to name content).
    std::unordered_map<uint64_t, uint32_t> byTexture;
    Stats counters;
    std::FILE* log = nullptr;
};

}  // namespace gt4::render
