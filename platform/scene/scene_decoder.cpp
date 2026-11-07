#include "scene_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt4::scene {

namespace rec = gt4::gfx::scene;

bool inverse(const Mat4& a, Mat4& r) {
    double m[4][8];
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) {
            m[i][k] = a.m[i][k];
            m[i][k + 4] = i == k ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int i = c + 1; i < 4; ++i)
            if (std::fabs(m[i][c]) > std::fabs(m[p][c])) p = i;
        if (std::fabs(m[p][c]) < 1e-12) {
            r = Mat4{};
            return false;
        }
        if (p != c)
            for (int k = 0; k < 8; ++k) std::swap(m[p][k], m[c][k]);
        const double d = m[c][c];
        for (int k = 0; k < 8; ++k) m[c][k] /= d;
        for (int i = 0; i < 4; ++i) {
            if (i == c) continue;
            const double f = m[i][c];
            for (int k = 0; k < 8; ++k) m[i][k] -= f * m[c][k];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) r.m[i][k] = static_cast<float>(m[i][k + 4]);
    return true;
}

// (a non-finite entry gives infinity, so it fails every tolerance: std::max would drop a NaN)
float largest(const Mat4& a) {
    float v = 0.0f;
    for (const auto& row : a.m)
        for (float x : row) {
            if (!std::isfinite(x)) return INFINITY;
            v = std::max(v, std::fabs(x));
        }
    return v;
}

float largestDifference(const Mat4& a, const Mat4& b) {
    float v = 0.0f;
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) {
            const float d = std::fabs(a.m[i][k] - b.m[i][k]);
            if (!std::isfinite(d)) return INFINITY;
            v = std::max(v, d);
        }
    return v;
}

void SceneCounters::add(const SceneCounters& o) {
    records += o.records; bad += o.bad; versionUnknown += o.versionUnknown; unknownKind += o.unknownKind;
    outsideFrame += o.outsideFrame; frames += o.frames; framesUnclosed += o.framesUnclosed;
    countsMismatch += o.countsMismatch; begins += o.begins; views += o.views; viewsNested += o.viewsNested;
    viewEndWithoutBegin += o.viewEndWithoutBegin; viewsUnclosed += o.viewsUnclosed; projections += o.projections; projectionUnknown += o.projectionUnknown;
    instances += o.instances; instancesWithoutView += o.instancesWithoutView; itemsMismatch += o.itemsMismatch;
    jobsTagged += o.jobsTagged; jobsUntaggedInView += o.jobsUntaggedInView; matrixChecked += o.matrixChecked;
    matrixMismatch += o.matrixMismatch; matrixMaxError = std::max(matrixMaxError, o.matrixMaxError);
    viewportMismatch += o.viewportMismatch; guardUnexpected += o.guardUnexpected; cameraChecked += o.cameraChecked;
    cameraMismatch += o.cameraMismatch; lights += o.lights; emitters += o.emitters; markers += o.markers;
    vehicles += o.vehicles; viewSplitTargets += o.viewSplitTargets; viewsUnbound += o.viewsUnbound;
    fallbackNotBound += o.fallbackNotBound; fallbackMeshPending += o.fallbackMeshPending;
    fallbackPipelinePending += o.fallbackPipelinePending; fallbackValidation += o.fallbackValidation;
    fallbackReadPending += o.fallbackReadPending; meshesNew += o.meshesNew; meshesPending += o.meshesPending;
    materialsNew += o.materialsNew;
}

namespace {
template <class T>
T as(const uint8_t* payload) {
    T r;
    std::memcpy(&r, payload, sizeof r);
    return r;
}
constexpr float kMatrixTolerance = 1e-5f;   // CONTRACTS.md a.7: relative to the largest entry
uint64_t cameraKey(uint8_t role, uint32_t pass) { return uint64_t(role) << 32 | pass; }
}  // namespace

void SceneDecoder::record(const uint8_t* payload, size_t bytes) {
    seen_ = true;   // from now on the stream tells the decoder about every run and job (the first frame included)
    // counters of a record outside any frame go straight to the totals
    SceneCounters loose;
    SceneCounters& c = frame_ ? frame_->counters : loose;
    struct Flush {
        SceneDecoder& d;
        SceneCounters& loose;
        ~Flush() { d.totals_.add(loose); }
    } flush{*this, loose};

    if (bytes < sizeof(rec::Prefix)) {
        ++c.records;
        ++c.bad;
        return;
    }
    const auto prefix = as<rec::Prefix>(payload);
    const uint32_t need = rec::recordSize(prefix.kind);
    if (need == 0 || prefix.version > rec::kVersion || bytes < need) {
        ++c.records;
        if (need == 0) ++c.unknownKind;
        else if (prefix.version > rec::kVersion) ++c.versionUnknown;
        else ++c.bad;
        return;
    }
    if (prefix.kind == rec::kFrameBegin) {
        beginFrame(as<rec::FrameBegin>(payload));
        ++frame_->counters.records;
        return;
    }
    if (!frame_) {
        ++loose.records;
        ++loose.outsideFrame;
        return;
    }
    ++c.records;
    switch (prefix.kind) {
    case rec::kFrameEnd: {
        const auto r = as<rec::FrameEnd>(payload);
        closeInstance();
        const SceneFrame& f = *frame_;
        if (r.frame != f.frame || r.views != f.views.size() || r.instances != f.instances.size() || r.lights != f.lights.size() ||
            r.emitters != f.emitters.size() || r.projections != f.projections.size())
            ++c.countsMismatch;
        frame_->begins = r.begins;
        c.begins += r.begins;
        endFrame(true);
        break;
    }
    case rec::kViewBegin: viewBegin(as<rec::ViewBegin>(payload)); break;
    case rec::kViewEnd: viewEnd(as<rec::ViewEnd>(payload)); break;
    case rec::kProjection: projection(as<rec::Projection>(payload)); break;
    case rec::kInstance: instance(as<rec::Instance>(payload)); break;
    case rec::kMarker: marker(as<rec::Marker>(payload)); break;
    case rec::kLight:
        ++c.lights;
        frame_->lights.push_back(as<rec::Light>(payload));
        break;
    case rec::kEmitter:
        ++c.emitters;
        frame_->emitters.push_back(as<rec::Emitter>(payload));
        break;
    case rec::kVehicle:
        ++c.vehicles;
        frame_->vehicles.push_back(as<rec::Vehicle>(payload));
        break;
    default: break;
    }
}

void SceneDecoder::beginFrame(const rec::FrameBegin& r) {
    if (frame_) {   // a FrameBegin before the last one's FrameEnd: that one is dropped
        ++frame_->counters.framesUnclosed;
        endFrame(false);
    }
    frame_ = std::make_unique<SceneFrame>();
    frame_->frame = r.frame;
    frame_->vblank = r.vblank;
    frame_->flags = r.flags;
    frame_->counters.frames = 1;
    view_ = instance_ = runInstance_ = -1;
    occurrences_.clear();
}

void SceneDecoder::endFrame(bool closed) {
    closeInstance();
    if (view_ >= 0) {   // a view without its ViewEnd
        ++frame_->counters.viewsUnclosed;
        closeView(false);
    }
    totals_.add(frame_->counters);
    if (closed) {
        frame_->complete = frame_->counters.countsMismatch == 0 && frame_->counters.viewsUnclosed == 0;
        done_ = std::move(frame_);
        lastCameras_ = std::move(cameras_);
        lastObjects_ = std::move(objects_);
    } else {   // a dropped frame breaks the sequence: the next one has no previous matrices
        frame_.reset();
        lastCameras_.clear();
        lastObjects_.clear();
    }
    cameras_.clear();
    objects_.clear();
    view_ = instance_ = runInstance_ = -1;
}

void SceneDecoder::projection(const rec::Projection& r) {
    SceneCounters& c = frame_->counters;
    ++c.projections;
    if (r.id != frame_->projections.size() + 1) ++c.bad;   // ids are 1.. in sending order (a.5 4a)
    ProjectionData p;
    p.id = r.id;
    p.view = r.view;
    p.viewToClip = Mat4::from(r.viewToClip);
    std::memcpy(p.viewportScale, r.viewportScale, sizeof p.viewportScale);
    std::memcpy(p.viewportOffset, r.viewportOffset, sizeof p.viewportOffset);
    std::memcpy(p.clamp, r.clamp, sizeof p.clamp);
    frame_->projections.push_back(p);
}

void SceneDecoder::viewBegin(const rec::ViewBegin& r) {
    SceneCounters& c = frame_->counters;
    closeInstance();
    if (view_ >= 0) {   // views do not nest (a.5 2)
        ++c.viewsNested;
        closeView(false);
    }
    ++c.views;
    if (r.view != frame_->views.size() + 1) ++c.bad;   // numbered from 1 in ViewBegin order (a.3)
    if (r.projection > frame_->projections.size()) ++c.projectionUnknown;
    ViewData v;
    v.id = r.view;
    v.role = r.role;
    v.flags = r.flags;
    v.pass = r.pass;
    v.projection = r.projection;
    v.worldToView = Mat4::from(r.worldToView);
    std::memcpy(v.scissor, r.scissor, sizeof v.scissor);
    const uint64_t key = cameraKey(r.role, r.pass);
    if (auto it = lastCameras_.find(key); it != lastCameras_.end()) {
        v.previousWorldToView = it->second;
        v.previousKnown = true;
    }
    cameras_[key] = v.worldToView;
    v.firstInstance = static_cast<uint32_t>(frame_->instances.size());
    v.firstLight = static_cast<uint32_t>(frame_->lights.size());
    frame_->views.push_back(v);
    view_ = static_cast<int>(frame_->views.size()) - 1;
}

void SceneDecoder::viewEnd(const rec::ViewEnd& r) {
    SceneCounters& c = frame_->counters;
    closeInstance();
    if (view_ < 0 || frame_->views[view_].id != r.view) {
        ++c.viewEndWithoutBegin;
        return;
    }
    if (r.instances != frame_->views[view_].instanceCount) ++c.countsMismatch;
    closeView(true);
}

void SceneDecoder::closeView(bool ended) {
    ViewData& v = frame_->views[view_];
    v.closed = ended;
    v.lightCount = static_cast<uint32_t>(frame_->lights.size()) - v.firstLight;
    view_ = -1;
}

void SceneDecoder::instance(const rec::Instance& r) {
    SceneCounters& c = frame_->counters;
    closeInstance();
    ++c.instances;
    if (r.serial != frame_->instances.size() + 1) ++c.bad;   // 1.. per frame in emission order
    InstanceData d;
    d.flags = r.flags;
    d.serial = r.serial;
    d.projection = r.projection;
    d.cmd = r.cmd;
    d.entrant = r.entrant;
    d.set = r.set;
    d.model = r.model;
    d.lod = r.lod;
    d.lodCount = r.lodCount;
    d.section = r.section;
    d.object = (r.flags & rec::kInstObjectKnown) ? (uint64_t(r.set) << 32 ^ uint64_t(r.model) << 8 ^ r.lod) : r.cmd;
    d.objectToView = Mat4::from(r.objectToView);
    std::memcpy(d.axisScale, r.axisScale, sizeof d.axisScale);
    d.shapeScale = r.shapeScale;
    d.items = r.items;
    // an id not sent yet, or none for a Cmd that draws (its jobs could not be checked)
    if (r.projection > frame_->projections.size() || (r.projection == 0 && r.items > 0)) ++c.projectionUnknown;
    ViewData* view = view_ >= 0 && frame_->views[view_].id == r.view && r.view != 0 ? &frame_->views[view_] : nullptr;
    if (!view) {
        ++c.instancesWithoutView;
        d.view = 0;
    } else {
        d.view = r.view;
        ++view->instanceCount;
        Mat4 viewToWorld;
        if (inverse(view->worldToView, viewToWorld)) {
            d.objectToWorld = d.objectToView * viewToWorld;
            // the Cmd's n-th occurrence this frame matches its n-th last frame (a Cmd drawn twice keeps both). Known
            // limit: when an earlier copy is culled in one frame, the later copies shift onto the wrong previous
            // matrix for that frame (one frame of wrong motion vectors on those copies)
            const uint64_t key = uint64_t(r.cmd) << 32 | occurrences_[r.cmd]++;
            if (auto it = lastObjects_.find(key); it != lastObjects_.end()) d.previousObjectToWorld = it->second;
            else d.previousObjectToWorld = d.objectToWorld;
            objects_[key] = d.objectToWorld;
        }
        if (r.flags & rec::kInstCourse) {   // course objects are in world space (R1): objectToView is the camera
            ++c.cameraChecked;
            const float scale = std::max(largest(view->worldToView), 1.0f);
            // a non-finite camera (scale infinite: inf <= inf would pass) or object matrix (NaN compares false) counts
            if (!std::isfinite(scale) || !(largestDifference(d.objectToView, view->worldToView) <= kMatrixTolerance * scale))
                ++c.cameraMismatch;
        }
    }
    frame_->instances.push_back(d);
    instance_ = static_cast<int>(frame_->instances.size()) - 1;
}

void SceneDecoder::closeInstance() {
    if (instance_ < 0 || !frame_) {
        instance_ = -1;
        return;
    }
    const InstanceData& d = frame_->instances[instance_];
    if (d.runs != d.items) {   // exactly `items` kBatchRun records follow (a.5 3)
        ++frame_->counters.itemsMismatch;
        // GT4_SCENE_DEBUG=1 (debugging): the first few, with the Cmd and its view
        static const bool debug = [] { const char* e = std::getenv("GT4_SCENE_DEBUG"); return e && e[0] == '1'; }();
        static int shown = 0;
        if (debug && shown < 8) {
            ++shown;
            const ViewData* v = d.view && d.view <= frame_->views.size() ? &frame_->views[d.view - 1] : nullptr;
            std::fprintf(stderr, "scene: frame %u instance %u (Cmd %08x, view %u role %d pass %u): %u items, %u runs\n", frame_->frame,
                         d.serial, d.cmd, d.view, v ? int(v->role) : -1, v ? v->pass : 0u, unsigned(d.items), unsigned(d.runs));
        }
    }
    instance_ = -1;
    runInstance_ = -1;
}

void SceneDecoder::marker(const rec::Marker& r) {
    closeInstance();
    ++frame_->counters.markers;
    frame_->markers.push_back(r);
}

void SceneDecoder::batchRun() {
    runInstance_ = -1;
    if (!frame_ || instance_ < 0) return;
    InstanceData& d = frame_->instances[instance_];
    if (d.runs < d.items) runInstance_ = instance_;   // one more beyond `items` is the instance's mistake: untagged
    ++d.runs;
}

void SceneDecoder::runEnd() { runInstance_ = -1; }

void SceneDecoder::job(gfx::ModelJob& job) {
    // the tag is the instance's position + 1, never the record's serial: a bad serial is counted, not trusted
    job.pad[0] = runInstance_ >= 0 ? static_cast<uint32_t>(runInstance_) + 1 : 0;
    if (!frame_) return;
    SceneCounters& c = frame_->counters;
    if (runInstance_ < 0) {
        if (view_ >= 0) ++c.jobsUntaggedInView;   // (also a job from a VIF-code MSCAL inside an open view)
        return;
    }
    ++c.jobsTagged;
    InstanceData& d = frame_->instances[static_cast<size_t>(runInstance_)];
    ++d.jobCount;
    if (job.guard[0] != 0.5f || job.guard[1] != 0.5f || job.guard[2] != 1.0f || job.guard[3] != 1.0f) ++c.guardUnexpected;
    if (d.projection == 0 || d.projection > frame_->projections.size()) return;   // (counted as projectionUnknown)
    const ProjectionData& p = frame_->projections[d.projection - 1];
    // ModelJob::matrix = diag(axisScale * k, 1) * objectToView * viewToClip, k = shapeScale or 1 when 0 (R1)
    Mat4 scale;
    const float k = d.shapeScale != 0.0f ? d.shapeScale : 1.0f;
    for (int i = 0; i < 3; ++i) scale.m[i][i] = d.axisScale[i] * k;
    const Mat4 expected = scale * d.objectToView * p.viewToClip;
    const Mat4 got = Mat4::from(job.matrix);
    ++c.matrixChecked;
    // a non-finite entry on either side is a mismatch: inf / inf would be NaN, which compares false with anything
    const float difference = largestDifference(expected, got), size = largest(got);
    const float error = std::isfinite(difference) && std::isfinite(size) ? difference / std::max(size, 1e-30f) : INFINITY;
    c.matrixMaxError = std::max(c.matrixMaxError, error);
    if (!(error <= kMatrixTolerance)) ++c.matrixMismatch;
    if (std::memcmp(p.viewportScale, job.scale, sizeof job.scale) != 0 || std::memcmp(p.viewportOffset, job.offset, sizeof job.offset) != 0 ||
        std::memcmp(p.clamp, job.clamp, sizeof job.clamp) != 0)
        ++c.viewportMismatch;
}

std::unique_ptr<SceneFrame> SceneDecoder::takeFrame() { return std::move(done_); }

}  // namespace gt4::scene
