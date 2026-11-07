#pragma once
// The scene decoder (docs/modern/CONTRACTS.md a.5, a.7; ticket A10): turns the kScene records the game sends with
// its batches into a SceneFrame per frame, tags each ModelJob of an instance's batch runs with the instance's serial
// (ModelJob::pad[0]), and checks the records against the batches they describe. Runs on the GS stream thread, in
// stream order: record() for each kScene payload, batchRun() before each kBatchRun runs, job() for each ModelJob the
// run produces. Nothing here is reached while the game sends no scene records (the option off).
#include "scene_frame.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace gt4::scene {

class SceneDecoder {
public:
    // One kScene payload: a gfx::scene::Prefix, then that kind's record (at least its size).
    void record(const uint8_t* payload, size_t bytes);
    // A kBatchRun is about to run: the jobs it produces belong to the open instance while that one has runs left.
    void batchRun();
    // That run is over: a job from anywhere else (a VIF-code MSCAL) is nobody's.
    void runEnd();
    // A ModelJob from the current run: tagged and checked against its instance. pad[0] = the instance's position in
    // SceneFrame::instances + 1 (Instance::serial in a well-formed stream), 0 for none.
    void job(gfx::ModelJob& job);
    // The frame FrameEnd closed, or null (then none since the last call). Older closed frames are dropped.
    std::unique_ptr<SceneFrame> takeFrame();
    std::unique_ptr<SceneFrame>& doneFrame() { return done_; }   // the last closed one, for the stream's target to take
    // The views' packages at their ViewEnd (CONTRACTS.md a.8), made only while asked for (a target that takes them).
    void setViewPackages(bool on) { packages_ = on; }
    std::shared_ptr<SceneView> takeClosedView() { return std::move(closedView_); }
    // Running totals of the frames finished so far (closed or dropped) and of records outside any frame.
    const SceneCounters& totals() const { return totals_; }
    bool seen() const { return seen_; }   // any scene record so far (the stream consults the decoder from then on)
    bool frameOpen() const { return frame_ != nullptr; }   // a FrameBegin not yet ended
    // The stream ended: a frame still open is counted as framesTruncated (a capture's last list, CONTRACTS.md a.5.1) and
    // dropped, its own counters not added to the totals.
    void finish();

private:
    SceneCounters& counters() { return frame_ ? frame_->counters : orphan_; }
    void closeInstance();
    void closeView(bool ended);   // ended: its ViewEnd arrived (else nested or left open at FrameEnd)
    void endFrame(bool closed);
    void beginFrame(const gfx::scene::FrameBegin& r);
    void viewBegin(const gfx::scene::ViewBegin& r);
    void viewEnd(const gfx::scene::ViewEnd& r);
    void projection(const gfx::scene::Projection& r);
    void instance(const gfx::scene::Instance& r);
    void marker(const gfx::scene::Marker& r);

    std::unique_ptr<SceneFrame> frame_;      // the frame being received
    std::unique_ptr<SceneFrame> done_;       // the last closed one, until taken
    SceneCounters orphan_;                   // counters of records outside a frame (added to the totals)
    SceneCounters totals_;
    bool seen_ = false;
    int view_ = -1;                          // the open view's index in frame_->views, -1 none
    int instance_ = -1;                      // the open instance's index, -1 none
    int runInstance_ = -1;                   // the instance the current run's jobs belong to, -1 none
    uint32_t viewErrors_ = 0;                // the open view's own errors so far (its verdict at ViewEnd: a.8)
    bool packages_ = false;
    std::shared_ptr<SceneView> closedView_;  // the package of the view that just ended, until the stream takes it
    int closed_ = -1;                        // the instance that closed with its `items` runs, until the next Instance,
                                             // ViewBegin / ViewEnd, Marker or frame (-1 none): runs after it are a.5.3's
    bool afterRun_ = false, afterRunSame_ = false;   // the current run came after closed_; one of its jobs had its matrix
    bool expectedMatrix(const InstanceData& d, Mat4& out) const;
    void package(const ViewData& v);         // the view's SceneView into closedView_   // diag(axisScale * k, 1) * objectToView * viewToClip
    // last frame's cameras by role + pass, and instances' objectToWorld by Cmd address and its occurrence in the frame
    // (a Cmd drawn twice: each copy keeps its own; the previous* fields)
    std::unordered_map<uint64_t, Mat4> lastCameras_, cameras_;
    std::unordered_map<uint64_t, Mat4> lastObjects_, objects_;
    std::unordered_map<uint32_t, uint32_t> occurrences_;   // this frame's count per Cmd address
};

}  // namespace gt4::scene
