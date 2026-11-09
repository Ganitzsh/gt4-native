// B16c's anchor (seam_anchor.h).
#include "seam_anchor.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace gt4::gsn {

namespace {

// A pass: a run of draws into one target sampling one source (consecutive among that target's draws), or a grade.
struct Pass {
    size_t first = 0, last = 0;
    uint32_t dest = 0, source = 0;   // source: the target sampled (0 none)
    enum Kind : uint8_t { kPlain, kTarget, kReinterpreted, kGrade, k3D } kind = kPlain;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;   // into dest, GS pixels
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;   // from source, its GS pixels
};

const Target* targetOf(const FrameList& list, uint32_t id) {
    for (const Target& t : list.targets)
        if (t.id == id) return &t;
    return nullptr;
}

bool covers(const Pass& p, float x0, float y0, float x1, float y1, float share) {
    const float ix = std::max(0.0f, std::min(p.x1, x1) - std::max(p.x0, x0));
    const float iy = std::max(0.0f, std::min(p.y1, y1) - std::max(p.y0, y0));
    const float area = (x1 - x0) * (y1 - y0);
    return area > 0.0f && ix * iy >= share * area;
}

std::vector<Pass> passesOf(const FrameList& list) {
    std::vector<Pass> passes;
    std::unordered_map<uint32_t, size_t> open;   // dest -> its open pass
    for (size_t at = 0; at < list.commands.size(); ++at) {
        const FrameList::Command& cmd = list.commands[at];
        if (cmd.kind == FrameList::Command::Kind::Effect && cmd.index < list.effects.size()) {
            const FrameList::Effect& e = list.effects[cmd.index];
            Pass p;
            p.first = p.last = at;
            p.dest = e.destination;
            p.source = e.source;
            p.kind = Pass::kGrade;
            p.x0 = p.u0 = float(e.x0), p.y0 = p.v0 = float(e.y0), p.x1 = p.u1 = float(e.x1 + 1), p.y1 = p.v1 = float(e.y1 + 1);
            passes.push_back(p);
            open.erase(e.destination);
            continue;
        }
        if (cmd.kind != FrameList::Command::Kind::Draw || cmd.index >= list.draws.size()) continue;
        const Draw& d = list.draws[cmd.index];
        Pass::Kind kind = Pass::kPlain;
        uint32_t source = 0;
        const bool textured = (d.state.prim & gfx::kTextured) != 0;
        if (d.modeled) {
            kind = Pass::k3D;
        } else if (textured && d.texture.kind == TextureSource::Kind::Target) {
            kind = Pass::kTarget, source = d.texture.id;
        } else if (textured && d.texture.kind == TextureSource::Kind::Reinterpreted && d.texture.id < list.reinterpretations.size() &&
                   list.reinterpretations[d.texture.id].target && !list.reinterpretations[d.texture.id].depth) {
            kind = Pass::kReinterpreted, source = list.reinterpretations[d.texture.id].target;
        }
        auto o = open.find(d.target);
        if (o == open.end() || passes[o->second].kind != kind || passes[o->second].source != source) {
            Pass p;
            p.first = at;
            p.dest = d.target;
            p.source = source;
            p.kind = kind;
            passes.push_back(p);
            o = open.insert_or_assign(d.target, passes.size() - 1).first;
        }
        Pass& p = passes[o->second];
        p.last = at;
        if (kind == Pass::k3D) continue;
        const gfx::Texture& tx = d.state.c.texture;
        const float tw = float(1u << std::min<uint32_t>(tx.log2Width, 10u)), th = float(1u << std::min<uint32_t>(tx.log2Height, 10u));
        const bool uv = (d.state.prim & gfx::kTexelCoords) != 0;
        for (uint32_t i = 0; i < d.count && size_t(d.first) + i < list.vertices.size(); ++i) {
            const Vertex& v = list.vertices[d.first + i];
            if (v.w != 0.0f) continue;   // (3D: not a copy)
            p.x0 = std::min(p.x0, v.x), p.y0 = std::min(p.y0, v.y), p.x1 = std::max(p.x1, v.x), p.y1 = std::max(p.y1, v.y);
            if (!source) continue;
            float u = uv ? v.u : (v.q != 0.0f ? v.s / v.q * tw : 0.0f), w = uv ? v.v : (v.q != 0.0f ? v.t / v.q * th : 0.0f);
            if (kind == Pass::kTarget) u += float(d.texture.offsetX), w += float(d.texture.offsetY);
            p.u0 = std::min(p.u0, u), p.v0 = std::min(p.v0, w), p.u1 = std::max(p.u1, u), p.v1 = std::max(p.v1, w);
        }
    }
    return passes;
}

}  // namespace

const char* seamOutcomeName(SeamAnchor::Outcome outcome) {
    switch (outcome) {
        case SeamAnchor::Outcome::Found: return "found";
        case SeamAnchor::Outcome::Fields: return "fields";
        case SeamAnchor::Outcome::NotShown: return "not shown";
        case SeamAnchor::Outcome::NoAnchor: return "no anchor";
        case SeamAnchor::Outcome::NoLight: return "no Light";
        case SeamAnchor::Outcome::Capped: return "capped";
    }
    return "?";
}

SeamAnchor findSeamAnchor(const FrameList& list) {
    SeamAnchor a;
    if (!list.displayed) {
        a.outcome = SeamAnchor::Outcome::NotShown;
        return a;
    }
    // The target this list makes for the display: its last write (a draw, or a grade's destination: GT4 may grade last)
    // into a target of the shown one's size and format (double buffering: the game draws into the back buffer while the
    // front one is shown), else the shown one.
    const uint32_t shown = list.display.target2 ? list.display.target2 : list.display.target1;
    a.made = shown;
    if (const Target* s = targetOf(list, shown))
        for (size_t at = list.commands.size(); at-- > 0;) {
            const FrameList::Command& cmd = list.commands[at];
            uint32_t written = 0;
            if (cmd.kind == FrameList::Command::Kind::Draw && cmd.index < list.draws.size()) written = list.draws[cmd.index].target;
            else if (cmd.kind == FrameList::Command::Kind::Effect && cmd.index < list.effects.size())
                written = list.effects[cmd.index].destination;
            if (!written) continue;
            const Target* t = targetOf(list, written);
            if (t && !t->depth && t->bw == s->bw && t->height == s->height && (t->psm == s->psm || (t->psm <= 1 && s->psm <= 1))) {
                a.made = t->id;
                break;
            }
        }
    if (list.display.record.interlaced && list.display.record.fieldMode) {
        a.outcome = SeamAnchor::Outcome::Fields;
        return a;
    }
    // The displayed rectangle (as GsnScene shows it).
    const gfx::DisplayCircuit& c = list.display.record.circuit[list.display.target2 ? 1 : 0];
    uint32_t w = c.magH ? c.clocks / c.magH : 0, h = c.lines;
    if (!w || !h || w > 1024 || h > 1024) w = 640, h = 448;
    a.rect[0] = float(c.x), a.rect[1] = float(c.y), a.rect[2] = a.rect[0] + float(w), a.rect[3] = a.rect[1] + float(h);

    const std::vector<Pass> passes = passesOf(list);
    long found = -1, lastCovering = -1;
    for (size_t i = passes.size(); i-- > 0;) {
        const Pass& p = passes[i];
        if (p.dest != a.made || p.kind == Pass::k3D || !covers(p, a.rect[0], a.rect[1], a.rect[2], a.rect[3], kSeamCover)) continue;
        if (lastCovering < 0) lastCovering = long(i);
        if (p.source) {
            found = long(i);
            break;
        }
    }
    if (found < 0) {
        a.outcome = SeamAnchor::Outcome::NoAnchor;
        return a;
    }
    a.at = passes[size_t(found)].last;
    a.lastCovering = found == lastCovering;
    // Back through the sources, composing each pass's map (its rectangle onto its texture coordinates, axis by axis).
    float scale[2] = {1.0f, 1.0f}, offset[2] = {0.0f, 0.0f};
    const Pass* p = &passes[size_t(found)];
    for (;;) {
        const float sx = p->x1 > p->x0 ? (p->u1 - p->u0) / (p->x1 - p->x0) : 1.0f;
        const float sy = p->y1 > p->y0 ? (p->v1 - p->v0) / (p->y1 - p->y0) : 1.0f;
        offset[0] = sx * offset[0] + p->u0 - sx * p->x0, scale[0] *= sx;
        offset[1] = sy * offset[1] + p->v0 - sy * p->y0, scale[1] *= sy;
        ++a.hops;
        long lit = -1;   // the source's last Light before this pass
        for (size_t at = 0; at < p->first; ++at)
            if (list.commands[at].kind == FrameList::Command::Kind::Light && list.commands[at].index < list.lights.size() &&
                list.lights[list.commands[at].index].target == p->source)
                lit = long(at);
        // The last pass into the source before this one: it must sample a target and cover what this one read. One
        // after the Light means the picture this pass read is that pass's, not the Light's: followed, not the Light.
        const Pass* next = nullptr;
        for (size_t i = passes.size(); i-- > 0;) {
            const Pass& q = passes[i];
            if (q.dest != p->source || q.last >= p->first || q.kind == Pass::k3D) continue;
            if (q.source && covers(q, p->u0, p->v0, p->u1, p->v1, 0.9f)) next = &q;
            break;
        }
        if (lit >= 0 && next && long(next->first) > lit) lit = -1;
        if (lit >= 0) {
            const FrameList::Lighting& l = list.lights[list.commands[size_t(lit)].index];
            a.outcome = SeamAnchor::Outcome::Found;
            a.lightAt = size_t(lit);
            a.light = l.target;
            a.lightDepth = l.depth;
            std::copy(scale, scale + 2, a.scale);
            std::copy(offset, offset + 2, a.offset);
            // The Z buffer as the anchor sees it: inverted in place or not (its last depth-tested draw before the anchor).
            for (size_t at = a.at; at-- > 0;) {
                const FrameList::Command& cmd = list.commands[at];
                if (cmd.kind == FrameList::Command::Kind::Draw && cmd.index < list.draws.size() && list.draws[cmd.index].depth == l.depth) {
                    a.depthInverted = list.draws[cmd.index].depthInverted;
                    break;
                }
            }
            return a;
        }
        if (a.hops >= kSeamHops) {
            a.outcome = SeamAnchor::Outcome::Capped;
            return a;
        }
        if (!next) {
            a.outcome = SeamAnchor::Outcome::NoLight;
            return a;
        }
        p = next;
    }
}

}  // namespace gt4::gsn
