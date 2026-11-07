// The scene decoder (platform/scene/scene_decoder.h; docs/modern/CONTRACTS.md a.5, a.7): synthetic record streams.
// A well-formed frame leaves every error counter at 0, tags its instances' jobs and passes the matrix, viewport,
// guard and camera checks; each counter then moves in a test of its own.
#include "scene_decoder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace rec = gt4::gfx::scene;
using gt4::scene::Mat4;
using gt4::scene::SceneCounters;
using gt4::scene::SceneDecoder;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

template <class T>
T make(uint16_t kind) {
    T r{};
    r.prefix = {kind, rec::kVersion};
    return r;
}
template <class T>
void put(SceneDecoder& d, const T& r) {
    d.record(reinterpret_cast<const uint8_t*>(&r), sizeof r);
}
void copy(float (&to)[4][4], const Mat4& m) { std::memcpy(to, m.m, sizeof to); }

// GT4's race view (R1): 60 x 47.85 degrees, near 0.1, far 1200, GL clip; viewport 640 x 448 around 2048.
Mat4 viewToClip() {
    Mat4 p;
    const float n = 0.1f, f = 1200.0f;
    p.m[0][0] = 1.7321f;
    p.m[1][1] = 2.2538f;
    p.m[2][2] = (f + n) / (n - f);
    p.m[2][3] = -1.0f;
    p.m[3][2] = 2 * f * n / (n - f);
    p.m[3][3] = 0.0f;
    return p;
}
// A camera at (22.9, 1, -414.6) yawed a quarter turn (R1's race camera), as world-to-view.
Mat4 worldToView() {
    Mat4 v;
    v.m[0][0] = 0; v.m[0][2] = -1;
    v.m[2][0] = 1; v.m[2][2] = 0;
    v.m[3][0] = 414.6f; v.m[3][1] = -0.99f; v.m[3][2] = 22.9f;
    return v;
}
Mat4 translate(float x, float y, float z) {
    Mat4 t;
    t.m[3][0] = x;
    t.m[3][1] = y;
    t.m[3][2] = z;
    return t;
}

constexpr float kScale[4] = {320.0f, -224.0f, -8388095.5f, 63.75f};
constexpr float kOffset[4] = {2048.0f, 2048.0f, 8388096.5f, 191.35f};
constexpr float kClamp[4] = {3.4028235e38f, 3.4028235e38f, 3.4028235e38f, 100.0f};

rec::Projection projection(uint16_t id, uint16_t view) {
    auto p = make<rec::Projection>(rec::kProjection);
    p.id = id;
    p.view = view;
    copy(p.viewToClip, viewToClip());
    std::memcpy(p.viewportScale, kScale, sizeof kScale);
    std::memcpy(p.viewportOffset, kOffset, sizeof kOffset);
    std::memcpy(p.clamp, kClamp, sizeof kClamp);
    return p;
}
rec::ViewBegin viewBegin(uint16_t view, uint16_t projection) {
    auto v = make<rec::ViewBegin>(rec::kViewBegin);
    v.view = view;
    v.role = rec::kViewMain;
    v.flags = rec::kViewCameraKnown;
    v.projection = projection;
    copy(v.worldToView, worldToView());
    return v;
}
rec::Instance instance(uint32_t serial, uint16_t view, const Mat4& objectToView, uint16_t items, uint16_t flags = 0,
                       float shapeScale = 0.0f) {
    auto i = make<rec::Instance>(rec::kInstance);
    i.view = view;
    i.flags = flags;
    i.serial = serial;
    i.cmd = 0x01000000u + serial * 0x40;
    i.entrant = 0xFFFFFFFFu;
    copy(i.objectToView, objectToView);
    i.axisScale[0] = i.axisScale[1] = i.axisScale[2] = 1.0f;
    i.shapeScale = shapeScale;
    i.items = items;
    i.section = 0xFFFF;
    i.projection = 1;
    return i;
}
// The job the batch would carry for an instance (CONTRACTS.md a.7's rule, R1's measurement).
gt4::gfx::ModelJob jobFor(const rec::Instance& i) {
    gt4::gfx::ModelJob j{};
    Mat4 d;
    const float k = i.shapeScale != 0.0f ? i.shapeScale : 1.0f;
    for (int r = 0; r < 3; ++r) d.m[r][r] = i.axisScale[r] * k;
    const Mat4 m = d * Mat4::from(i.objectToView) * viewToClip();
    std::memcpy(j.matrix, m.m, sizeof j.matrix);
    std::memcpy(j.scale, kScale, sizeof kScale);
    std::memcpy(j.offset, kOffset, sizeof kOffset);
    std::memcpy(j.clamp, kClamp, sizeof kClamp);
    const float guard[4] = {0.5f, 0.5f, 1.0f, 1.0f};
    std::memcpy(j.guard, guard, sizeof guard);
    j.pad[0] = 0xDEADBEEFu;   // whatever the renderers left: the decoder overwrites it
    return j;
}

rec::FrameBegin frameBegin(uint32_t frame) {
    auto f = make<rec::FrameBegin>(rec::kFrameBegin);
    f.frame = frame;
    return f;
}
rec::FrameEnd frameEnd(uint32_t frame, uint16_t views, uint16_t instances, uint16_t projections) {
    auto e = make<rec::FrameEnd>(rec::kFrameEnd);
    e.frame = frame;
    e.views = views;
    e.instances = instances;
    e.projections = projections;
    return e;
}
rec::ViewEnd viewEnd(uint16_t view, uint16_t instances) {
    auto e = make<rec::ViewEnd>(rec::kViewEnd);
    e.view = view;
    e.instances = instances;
    return e;
}

// One well-formed race frame: a projection, the main view, a course piece (world space: objectToView is the camera,
// two items), a car body (moved, shape-scaled, one item), an untagged immediate batch in the view, the frame's end.
// Returns the jobs' tags in order.
std::vector<uint32_t> goodFrame(SceneDecoder& d, uint32_t frame) {
    std::vector<uint32_t> tags;
    auto run = [&](const rec::Instance* i) {   // as the stream does it: batchRun, the run's job, runEnd
        d.batchRun();
        gt4::gfx::ModelJob j = i ? jobFor(*i) : gt4::gfx::ModelJob{};
        j.pad[0] = 0xFFFFFFFFu;   // the decoder must write it either way
        d.job(j);
        d.runEnd();
        tags.push_back(j.pad[0]);
    };
    put(d, frameBegin(frame));
    put(d, projection(1, 0));
    put(d, viewBegin(1, 1));
    const auto course = instance(1, 1, worldToView(), 2, rec::kInstCourse);
    put(d, course);
    run(&course);
    run(&course);
    const auto car = instance(2, 1, translate(1.0f, 0.1f, 6.0f) * worldToView(), 1, rec::kInstCarBody, 1.25f);
    put(d, car);
    run(&car);
    put(d, viewEnd(1, 2));
    run(nullptr);   // a glow after the view: outside any instance or view
    put(d, frameEnd(frame, 1, 2, 1));
    return tags;
}

void wellFormed() {
    SceneDecoder d;
    const auto tags = goodFrame(d, 1);
    check(tags == std::vector<uint32_t>({1, 1, 2, 0}), "well formed: jobs tagged with their instance's serial, 0 outside");
    auto f = d.takeFrame();
    check(f && f->complete, "well formed: the frame closes complete");
    const SceneCounters& c = f ? f->counters : SceneCounters{};
    check(c.errorFree(), "well formed: every error counter 0");
    // FrameBegin, Projection, ViewBegin, two Instances, ViewEnd, FrameEnd
    check(c.records == 7 && c.views == 1 && c.instances == 2 && c.projections == 1, "well formed: counted");
    check(c.matrixChecked == 3 && c.matrixMismatch == 0 && c.matrixMaxError < 1e-6f, "well formed: every tagged matrix checked and right");
    check(c.cameraChecked == 1 && c.cameraMismatch == 0 && c.guardUnexpected == 0 && c.jobsUntaggedInView == 0,
          "well formed: camera, guard, untagged");
    if (f && f->instances.size() == 2) {
        const auto& car = f->instances[1];
        check(std::fabs(car.objectToWorld.m[3][0] - 1.0f) < 1e-4f && std::fabs(car.objectToWorld.m[3][2] - 6.0f) < 1e-4f,
              "well formed: objectToWorld = objectToView * inverse(worldToView)");
        check(car.runs == 1 && car.jobCount == 1 && f->instances[0].runs == 2, "well formed: runs and jobs per instance");
    }
    check(!d.takeFrame(), "well formed: a frame is taken once");
    // the next frame finds the camera of the last one (matched by role and pass) and the car's last position
    goodFrame(d, 2);
    f = d.takeFrame();
    check(f && f->views.size() == 1 && f->views[0].previousKnown, "next frame: previousWorldToView matched by role + pass");
    check(d.totals().frames == 2 && d.totals().errorFree(), "totals: two clean frames");
}

void decoding() {
    {   // a payload shorter than a prefix
        SceneDecoder d;
        const uint16_t two = rec::kMarker;
        d.record(reinterpret_cast<const uint8_t*>(&two), sizeof two);
        check(d.totals().bad == 1, "bad: shorter than a prefix");
    }
    {   // shorter than its kind's struct
        SceneDecoder d;
        put(d, frameBegin(1));
        auto v = viewBegin(1, 0);
        d.record(reinterpret_cast<const uint8_t*>(&v), sizeof v - 4);
        put(d, frameEnd(1, 0, 0, 0));
        check(d.totals().bad == 1, "bad: shorter than its kind's struct");
    }
    {   // a version newer than this host knows
        SceneDecoder d;
        auto f = frameBegin(1);
        f.prefix.version = rec::kVersion + 1;
        put(d, f);
        check(d.totals().versionUnknown == 1, "versionUnknown");
    }
    {   // an unknown kind
        SceneDecoder d;
        auto f = frameBegin(1);
        f.prefix.kind = 99;
        put(d, f);
        check(d.totals().unknownKind == 1, "unknownKind");
    }
    {   // a record outside any frame
        SceneDecoder d;
        put(d, viewBegin(1, 0));
        check(d.totals().outsideFrame == 1, "outsideFrame");
    }
}

void frames() {
    {   // a FrameBegin before the last FrameEnd: the last frame is dropped
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, frameBegin(2));
        put(d, frameEnd(2, 0, 0, 0));
        check(d.totals().framesUnclosed == 1, "framesUnclosed");
        auto f = d.takeFrame();
        check(f && f->frame == 2, "framesUnclosed: the new frame goes on");
    }
    {   // FrameEnd's counts differ from what arrived
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, frameEnd(1, 1, 0, 0));
        auto f = d.takeFrame();
        check(d.totals().countsMismatch == 1 && f && !f->complete, "countsMismatch: FrameEnd");
    }
    {   // ViewEnd's instance count differs
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        put(d, viewEnd(1, 3));
        put(d, frameEnd(1, 1, 0, 1));
        check(d.totals().countsMismatch == 1, "countsMismatch: ViewEnd");
    }
}

void views() {
    {   // a ViewBegin inside an open view
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        put(d, viewBegin(2, 1));
        put(d, viewEnd(2, 0));
        put(d, frameEnd(1, 2, 0, 1));
        check(d.totals().viewsNested == 1 && d.totals().viewEndWithoutBegin == 0, "viewsNested");
    }
    {   // a ViewEnd with no view open, and one for another view
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, viewEnd(1, 0));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        put(d, viewEnd(5, 0));
        put(d, viewEnd(1, 0));
        put(d, frameEnd(1, 1, 0, 1));
        check(d.totals().viewEndWithoutBegin == 2, "viewEndWithoutBegin");
    }
    {   // a projection named before it was sent
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, viewBegin(1, 4));
        auto i = instance(1, 1, worldToView(), 0);
        i.projection = 3;
        put(d, i);
        put(d, viewEnd(1, 1));
        put(d, frameEnd(1, 1, 1, 0));
        check(d.totals().projectionUnknown == 2, "projectionUnknown: ViewBegin and Instance");
    }
}

void instances() {
    {   // an instance outside any view
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, instance(1, 0, worldToView(), 0));
        put(d, frameEnd(1, 0, 1, 1));
        check(d.totals().instancesWithoutView == 1, "instancesWithoutView");
    }
    {   // fewer runs than items, then more
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        put(d, instance(1, 1, worldToView(), 2));
        d.batchRun();
        put(d, instance(2, 1, worldToView(), 1));
        d.batchRun();
        gt4::gfx::ModelJob j{};
        j.pad[0] = 0xFFFFFFFFu;
        d.batchRun();   // a run beyond its items: untagged
        d.job(j);
        put(d, viewEnd(1, 2));
        put(d, frameEnd(1, 1, 2, 1));
        check(d.totals().itemsMismatch == 2, "itemsMismatch: fewer and more runs than items");
        check(j.pad[0] == 0 && d.totals().jobsUntaggedInView == 1, "a run beyond the items stays untagged");
    }
}

void checks() {
    auto oneJob = [](gt4::gfx::ModelJob (*change)(gt4::gfx::ModelJob), uint16_t flags, const Mat4& objectToView) {
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        const auto i = instance(1, 1, objectToView, 1, flags);
        put(d, i);
        d.batchRun();
        gt4::gfx::ModelJob j = change(jobFor(i));
        d.job(j);
        put(d, viewEnd(1, 1));
        put(d, frameEnd(1, 1, 1, 1));
        return d.totals();
    };
    const auto none = [](gt4::gfx::ModelJob j) { return j; };
    check(oneJob(none, 0, worldToView()).errorFree(), "checks: the unchanged job passes");
    check(oneJob([](gt4::gfx::ModelJob j) { j.matrix[3][0] += 0.5f; return j; }, 0, worldToView()).matrixMismatch == 1,
          "matrixMismatch");
    // the tolerance is relative to the largest entry (about 718 here): 0.005 is 7e-6 of it and passes, which an
    // absolute 1e-5 would not
    check(oneJob([](gt4::gfx::ModelJob j) { j.matrix[3][1] += 0.005f; return j; }, 0, worldToView()).matrixMismatch == 0,
          "matrix: a large absolute but small relative difference passes");
    check(oneJob([](gt4::gfx::ModelJob j) { j.matrix[1][1] = NAN; return j; }, 0, worldToView()).matrixMismatch == 1,
          "matrixMismatch: a NaN entry");
    check(oneJob([](gt4::gfx::ModelJob j) { j.offset[1] = 1873.0f; return j; }, 0, worldToView()).viewportMismatch == 1,
          "viewportMismatch: offset");
    check(oneJob([](gt4::gfx::ModelJob j) { j.clamp[3] = 30.0f; return j; }, 0, worldToView()).viewportMismatch == 1,
          "viewportMismatch: the fog clamp");
    check(oneJob([](gt4::gfx::ModelJob j) { j.guard[0] = 0.25f; return j; }, 0, worldToView()).guardUnexpected == 1,
          "guardUnexpected");
    const auto moved = oneJob(none, rec::kInstCourse, translate(3.0f, 0.0f, 0.0f) * worldToView());
    check(moved.cameraChecked == 1 && moved.cameraMismatch == 1, "cameraMismatch: a course instance off the camera");
    {   // a NaN in the camera itself (worldToView): the course instance carries the same NaN, and it still counts
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        auto v = viewBegin(1, 1);
        v.worldToView[2][2] = NAN;
        put(d, v);
        auto i = instance(1, 1, worldToView(), 0, rec::kInstCourse);
        i.objectToView[2][2] = NAN;
        put(d, i);
        put(d, viewEnd(1, 1));
        put(d, frameEnd(1, 1, 1, 1));
        check(d.totals().cameraChecked == 1 && d.totals().cameraMismatch == 1, "cameraMismatch: a NaN in the camera");
    }
    {   // an untagged job inside an open view
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        d.batchRun();
        gt4::gfx::ModelJob j{};
        j.pad[0] = 0xFFFFFFFFu;
        d.job(j);
        put(d, viewEnd(1, 0));
        put(d, frameEnd(1, 1, 0, 1));
        check(d.totals().jobsUntaggedInView == 1 && j.pad[0] == 0, "jobsUntaggedInView, and its pad written 0");
    }
}
void robustness() {
    {   // a serial far beyond the frame's instances: counted bad, jobs tagged by position, nothing read out of bounds
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        auto i = instance(1000, 1, worldToView(), 1);
        put(d, i);
        d.batchRun();
        gt4::gfx::ModelJob j = jobFor(i);
        d.job(j);
        d.runEnd();
        put(d, viewEnd(1, 1));
        put(d, frameEnd(1, 1, 1, 1));
        check(d.totals().bad == 1 && j.pad[0] == 1 && d.totals().matrixMismatch == 0, "a bad serial: counted, tagged by position");
    }
    {   // projection 0 on a Cmd that draws: its jobs can't be checked, so it is counted
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        auto i = instance(1, 1, worldToView(), 1);
        i.projection = 0;
        put(d, i);
        d.batchRun();
        gt4::gfx::ModelJob j = jobFor(i);
        j.matrix[0][0] = 1e6f;   // nothing checks it
        d.job(j);
        d.runEnd();
        put(d, viewEnd(1, 1));
        put(d, frameEnd(1, 1, 1, 1));
        check(d.totals().projectionUnknown == 1 && !d.totals().errorFree(), "projection 0 on a drawing Cmd: projectionUnknown");
    }
    {   // a view still open at FrameEnd
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        put(d, frameEnd(1, 1, 0, 1));
        auto f = d.takeFrame();
        check(d.totals().viewsUnclosed == 1 && f && !f->complete && !f->views[0].closed, "viewsUnclosed");
    }
    {   // a run's tag ends with it: a job after runEnd (a VIF-code MSCAL) is nobody's
        SceneDecoder d;
        put(d, frameBegin(1));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        put(d, instance(1, 1, worldToView(), 1));
        d.batchRun();
        d.runEnd();
        gt4::gfx::ModelJob j{};
        j.pad[0] = 0xFFFFFFFFu;
        d.job(j);
        check(j.pad[0] == 0 && d.totals().jobsUntaggedInView == 0, "runEnd: a later job is untagged");
        put(d, viewEnd(1, 1));
        put(d, frameEnd(1, 1, 1, 1));
        check(d.totals().jobsUntaggedInView == 1, "runEnd: that job, inside the open view, counts as untagged in view");
    }
}

// previousObjectToWorld follows each copy of a Cmd from frame to frame; a dropped frame leaves no previous.
void history() {
    SceneDecoder d;
    auto frameWith = [&](uint32_t n, float x) {
        put(d, frameBegin(n));
        put(d, projection(1, 0));
        put(d, viewBegin(1, 1));
        auto a = instance(1, 1, translate(x, 0, 0) * worldToView(), 0);          // the car, moving
        auto b = instance(2, 1, translate(100, 0, 0) * worldToView(), 0);        // the same Cmd drawn again elsewhere
        b.cmd = a.cmd;
        put(d, a);
        put(d, b);
        put(d, viewEnd(1, 2));
        put(d, frameEnd(n, 1, 2, 1));
        return d.takeFrame();
    };
    frameWith(1, 5.0f);
    auto f = frameWith(2, 6.0f);
    check(f && std::fabs(f->instances[0].previousObjectToWorld.m[3][0] - 5.0f) < 1e-3f &&
              std::fabs(f->instances[0].objectToWorld.m[3][0] - 6.0f) < 1e-3f,
          "previousObjectToWorld: last frame's position of a moving instance");
    check(f && std::fabs(f->instances[1].previousObjectToWorld.m[3][0] - 100.0f) < 1e-3f,
          "previousObjectToWorld: a Cmd drawn twice keeps each copy's own");
    put(d, frameBegin(3));   // dropped by the next FrameBegin
    f = frameWith(4, 7.0f);
    check(f && !f->views[0].previousKnown && std::fabs(f->instances[0].previousObjectToWorld.m[3][0] - 7.0f) < 1e-3f,
          "after a dropped frame: no previous camera or object matrices");
}

// The counters no other test moves.
void moreCounters() {
    SceneDecoder d;
    put(d, frameBegin(1));
    put(d, projection(1, 0));
    put(d, viewBegin(1, 1));
    put(d, make<rec::Light>(rec::kLight));
    put(d, viewEnd(1, 0));
    put(d, make<rec::Emitter>(rec::kEmitter));
    put(d, make<rec::Vehicle>(rec::kVehicle));
    put(d, make<rec::Marker>(rec::kMarker));
    auto e = frameEnd(1, 1, 0, 1);
    e.lights = 1;
    e.emitters = 1;
    put(d, e);
    const SceneCounters& c = d.totals();
    check(c.lights == 1 && c.emitters == 1 && c.vehicles == 1 && c.markers == 1 && c.errorFree(), "lights, emitters, vehicles, markers");
    auto f = d.takeFrame();
    check(f && f->views[0].lightCount == 1 && f->lights.size() == 1 && f->vehicles.size() == 1, "kept in the frame");
}

}  // namespace

int main() {
    wellFormed();
    decoding();
    frames();
    views();
    instances();
    checks();
    robustness();
    history();
    moreCounters();
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("scene_decoder: all checks passed\n");
    return failures ? 1 : 0;
}
