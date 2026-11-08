// The light gather (lights_gather.h).
#include "lights_gather.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "light_sources.h"

namespace gt4::render {

namespace {

bool finite3(const float (&v)[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// The game's display-referred colour into linear light. Colours above 1 (the game's, before its clamp) keep their
// excess through the same curve.
float linearise(float c) { return c > 0.0f ? std::pow(c, 2.2f) : 0.0f; }

}  // namespace

LightListHeader gatherLights(const gfx::scene::Light* records, size_t count, uint16_t view, float zNear, float zFar,
                             float scale, std::vector<GpuLight>& out, GatherStats& stats) {
    using namespace gfx::scene;
    out.clear();
    stats = {};
    std::vector<GpuLight> directional, local;
    for (size_t i = 0; i < count; ++i) {
        const Light& r = records[i];
        if (r.view != view) continue;
        ++stats.records;
        const SourceDefaults d = sourceDefaults(r.source);
        if (!d.known) {
            ++stats.unknownSource;
            continue;
        }
        if (!d.lights) {
            ++stats.notLights;
            continue;
        }
        const bool isDirectional = r.kind == kLightDirectional;
        if (!finite3(r.colour) || !std::isfinite(r.intensity) || (!isDirectional && !finite3(r.position))) {
            ++stats.invalid;
            continue;
        }
        GpuLight g{};
        const float k = std::max(r.intensity, 0.0f) * d.intensity * scale;
        for (int c = 0; c < 3; ++c) g.colour[c] = linearise(r.colour[c]) * k;
        if (!(std::max({g.colour[0], g.colour[1], g.colour[2]}) > 0.0f)) {
            ++stats.dark;   // an off lamp or a blink's dark half: legitimate, counted apart
            continue;
        }
        for (int c = 0; c < 3; ++c) g.position[c] = isDirectional ? 0.0f : r.position[c];
        // A direction (spot, directional): made unit. A degenerate one: a directional light keeps pointing down the
        // view (-z, as GT4's view looks), and a spot lights as a point (below).
        float dir[3] = {0.0f, 0.0f, -1.0f};
        bool hasDirection = false;
        if (finite3(r.direction)) {
            const float len = std::sqrt(r.direction[0] * r.direction[0] + r.direction[1] * r.direction[1] +
                                        r.direction[2] * r.direction[2]);
            if (len > 1e-6f) {
                for (int c = 0; c < 3; ++c) dir[c] = r.direction[c] / len;
                hasDirection = true;
            }
        }
        std::memcpy(g.direction, dir, sizeof dir);
        // A spot needs its cone and its direction; without either (cones 0, 0: unknown) it lights as a point.
        const bool cone = r.kind == kLightSpot && hasDirection && r.cosOuter > 0.0f && r.cosInner >= r.cosOuter &&
                          r.cosInner <= 1.0f;
        g.type = isDirectional ? kDirectional : (cone ? kSpot : kPoint);
        g.spotScale = cone ? spotScale(r.cosInner, r.cosOuter) : 0.0f;
        g.spotOffset = cone ? spotOffset(r.cosInner, r.cosOuter) : 0.0f;
        g.range = isDirectional ? 0.0f : ((r.range > 0.0f && std::isfinite(r.range)) ? r.range : d.range);
        g.flags = kFromGame | ((r.width > 0.0f || r.height > 0.0f) ? kGameGlow : 0u);
        g.sourceRadius = d.sourceRadius;
        g.id = r.id;
        (isDirectional ? directional : local).push_back(g);
    }
    // Over the limit: the point and spot lights whose sphere of `range` is nearest the camera are kept
    // (max(0, distance - range); ties, e.g. every sphere around the camera: the brighter), directional ones always.
    const size_t room = kMaxLights > directional.size() ? kMaxLights - directional.size() : 0;
    if (local.size() > room) {
        auto distance = [](const GpuLight& l) {
            return std::sqrt(l.position[0] * l.position[0] + l.position[1] * l.position[1] + l.position[2] * l.position[2]);
        };
        auto gap = [&](const GpuLight& l) { return std::max(0.0f, distance(l) - l.range); };
        auto luminance = [](const GpuLight& l) { return 0.2126f * l.colour[0] + 0.7152f * l.colour[1] + 0.0722f * l.colour[2]; };
        std::stable_sort(local.begin(), local.end(), [&](const GpuLight& a, const GpuLight& b) {
            const float ga = gap(a), gb = gap(b);
            if (ga != gb) return ga < gb;
            return luminance(a) > luminance(b);
        });
        stats.dropped = static_cast<uint32_t>(local.size() - room);
        for (size_t i = room; i < local.size(); ++i) stats.droppedAround += distance(local[i]) < local[i].range ? 1u : 0u;
        local.resize(room);
    }
    if (directional.size() > kMaxLights) {
        stats.dropped += static_cast<uint32_t>(directional.size() - kMaxLights);
        directional.resize(kMaxLights);
    }
    out = directional;
    out.insert(out.end(), local.begin(), local.end());
    stats.kept = static_cast<uint32_t>(out.size());
    return makeHeader(stats.kept, static_cast<uint32_t>(directional.size()), zNear, zFar);
}

std::vector<gfx::scene::Light> testLights(uint16_t view, uint32_t n) {
    using namespace gfx::scene;
    std::vector<Light> out;
    for (uint32_t i = 0; i < n; ++i) {
        Light l{};
        l.prefix = {kLight, kVersion};
        l.view = view;
        l.kind = kLightPoint;
        l.source = kSrcCourseGlow;
        l.id = i + 1;
        l.entrant = 0xFFFFFFFFu;
        l.position[0] = (i % 2) ? 7.0f : -7.0f;
        l.position[1] = 4.0f;
        l.position[2] = -(6.0f + 12.0f * float(i / 2));
        l.direction[1] = -1.0f;
        l.colour[0] = 1.0f;
        l.colour[1] = 0.85f;
        l.colour[2] = 0.6f;
        l.intensity = 1.0f;
        out.push_back(l);
    }
    return out;
}

uint32_t testLightCount() {
    static const uint32_t n = [] {
        const char* e = std::getenv("GT4_NIGHT_TEST_LIGHTS");
        const long v = e ? std::strtol(e, nullptr, 10) : 0;
        return v > 0 ? static_cast<uint32_t>(std::min<long>(v, kMaxLights)) : 0u;
    }();
    return n;
}

}  // namespace gt4::render
