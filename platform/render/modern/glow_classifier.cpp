// F2b's glow class (glow_classifier.h).
#include "glow_classifier.h"

namespace gt4::render {

bool glowSignature(const scene::ItemSource& item) {
    const gfx::Context* c = item.context;
    if (!c || !item.batch.job || !item.texture) return false;
    const bool textured = (item.prim >> 4) & 1, blended = (item.prim >> 6) & 1;
    const bool blend = c->blendA == 0 && c->blendB == 1 && c->blendC == 0 && c->blendD == 1;   // (Cs - Cd) As + Cd
    const bool core = c->alphaTest && c->alphaFunc == 4 && c->alphaRef == 0x80;                 // EQUAL 0x80
    const bool unlit = (item.batch.job->chain >> 16) == 0;                                       // lighting body 0
    return textured && blended && blend && core && c->depthWrite && unlit;
}

GlowScores glowTexture(const scene::TextureSummary& s) {
    GlowScores g;
    if (!s.known) return g;
    g.centreToRim = s.centreToRim;
    g.alphaCore = s.alphaCore / 255.0f;
    // A glow: its centre at least three times its rim, and some texels at alpha 0x80 (the core the alpha test passes).
    g.glow = g.centreToRim >= 3.0f && s.alphaCore > 0;
    return g;
}

uint32_t GlowClassifier::classify(const scene::ItemSource& item, const scene::TextureSummary* summary) {
    std::lock_guard<std::mutex> hold(lock);
    ++counters.judged;
    if (!glowSignature(item)) return 0;
    ++counters.signature;
    if (!summary || !summary->known) {
        ++counters.noSummary;
        return 0;
    }
    auto [it, fresh] = byTexture.try_emplace(item.texture, 0u);
    if (fresh) {
        ++counters.textures;
        const GlowScores scores = glowTexture(*summary);
        it->second = scores.glow ? glowClasses(summary->mean) : 0u;
        if (log)
            std::fprintf(log, "F2b glow class: texture %016llx %ux%u centre/rim %.2f core %.3f mean %u,%u,%u: %s\n",
                         static_cast<unsigned long long>(item.texture), summary->width, summary->height, scores.centreToRim,
                         scores.alphaCore, summary->mean[0], summary->mean[1], summary->mean[2], scores.glow ? "glow" : "not a glow");
    }
    if (it->second) ++counters.glows;
    return it->second;
}

GlowClassifier::Stats GlowClassifier::stats() const {
    std::lock_guard<std::mutex> hold(lock);
    return counters;
}

}  // namespace gt4::render
