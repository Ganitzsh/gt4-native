// F2b's glow class (glow_classifier.h).
#include "glow_classifier.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
    // A glow: its centre at least three times its rim. (No opaque core required: on lane A's step 1b capture the
    // street-lamp billboards' glow texture has no texel at alpha 0x80, centre/rim 406; the one flat texture with the
    // lamp state there, a sign, has 0.77.)
    g.glow = g.centreToRim >= 3.0f;
    return g;
}

// N3 step 0 (local, GT4_N3_LOG=1): every material with the lit-road layer's state (textured, (Cs - Cd) As + Cd, no
// alpha test, z GEQUAL, no z write, body 0): its texture summary and its first item's vertex colours, once.
static void logLitRoadCandidate(const scene::ItemSource& item, const scene::TextureSummary* summary) {
    static const bool on = [] { const char* e = std::getenv("GT4_N3_LOG"); return e && *e == '1'; }();
    const gfx::Context* c = item.context;
    if (!on || !c || !item.batch.job || !item.batch.words) return;
    const bool state = ((item.prim >> 4) & 1) && ((item.prim >> 6) & 1) && c->blendA == 0 && c->blendB == 1 && c->blendC == 0 &&
                       c->blendD == 1 && !c->alphaTest && c->depthTest && c->depthFunc == 2 && !c->depthWrite &&
                       (item.batch.job->chain >> 16) == 0;
    if (!state) return;
    const scene::DecodedBatch b = scene::decodeBatch(*item.batch.job, item.batch.words);
    double sum[4] = {}, sq[4] = {};
    for (const scene::MeshVertex& v : b.vertices)
        for (int k = 0; k < 4; ++k) {
            const double x = double((v.colour >> (8 * k)) & 0xFF);
            sum[k] += x;
            sq[k] += x * x;
        }
    const double n = b.vertices.empty() ? 1.0 : double(b.vertices.size());
    double mean[4], sd[4];
    for (int k = 0; k < 4; ++k) mean[k] = sum[k] / n, sd[k] = std::sqrt(std::max(0.0, sq[k] / n - mean[k] * mean[k]));
    std::printf("N3 candidate: texture %016llx %ux%u mean %u,%u,%u centre/rim %.2f core %.3f | renderer %u vertices %zu "
                "colour mean %.1f,%.1f,%.1f a %.1f sd %.1f,%.1f,%.1f a %.1f tfx %u tcc %u\n",
                static_cast<unsigned long long>(item.texture), summary ? summary->width : 0u, summary ? summary->height : 0u,
                summary ? summary->mean[0] : 0u, summary ? summary->mean[1] : 0u, summary ? summary->mean[2] : 0u,
                summary ? summary->centreToRim : 0.0f, summary ? summary->alphaCore / 255.0f : 0.0f, item.batch.job->renderer,
                b.vertices.size(), mean[0], mean[1], mean[2], mean[3], sd[0], sd[1], sd[2], sd[3], unsigned(c->texture.function),
                unsigned(c->texture.useAlpha));
}

uint32_t GlowClassifier::classify(const scene::ItemSource& item, const scene::TextureSummary* summary) {
    std::lock_guard<std::mutex> hold(lock);
    logLitRoadCandidate(item, summary);
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
