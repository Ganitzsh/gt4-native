#pragma once
// --record PATH: a real-time recording of what the host presents, with the game's sound, through an ffmpeg the user
// has (docs/RECORDING.md). Off by default; nothing here runs without --record. The renderer hands over each presented
// frame once the GPU has finished it (no wait in the frame); a writer thread pipes the frames to ffmpeg as raw video
// and appends the sound to a file beside the output; stop() then muxes the two into PATH. A frame the writer can't
// take in time (ffmpeg behind) is dropped and counted, never waited for, so recording doesn't slow the game.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gt4 {

// The pacing of a fixed-rate video from frames presented at times (seconds from the first): each frame is written as
// many times as output frames fell due since the one before (1 usually; 0 for a frame drawn faster than the rate;
// 2 or more fill a late frame's gap by repeating it), so the video keeps real time and stays with the sound.
class FramePacer {
public:
    explicit FramePacer(double fps) : fps_(fps) {}
    uint64_t copies(double seconds) {
        const uint64_t due = static_cast<uint64_t>(seconds < 0.0 ? 0.0 : seconds * fps_) + 1;
        const uint64_t n = due > written_ ? due - written_ : 0;
        written_ += n;
        return n;
    }
    uint64_t written() const { return written_; }

private:
    double fps_;
    uint64_t written_ = 0;
};

class Recorder {
public:
    struct Options {
        std::string path;                 // the recording (its extension picks the container: .mkv, .mp4...)
        std::string ffmpeg = "ffmpeg";    // the executable (--record-ffmpeg; else ffmpeg from PATH)
        std::string videoArgs;            // the video encoder's options (--record-args; empty: kDefaultVideoArgs)
        double fps = 60.0;
    };
    static constexpr const char* kDefaultVideoArgs = "-c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p";

    explicit Recorder(Options options);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // A presented frame: `width` x `height` pixels, rows of width * 4 bytes, in `vkFormat` (B8G8R8A8, R8G8B8A8 or
    // A2B10G10R10 / A2R10G10B10, UNORM or SRGB), presented `seconds` after any fixed origin (the first frame's starts
    // the recording). The render thread's call; copies the pixels.
    void frame(const uint8_t* pixels, uint32_t width, uint32_t height, int vkFormat, double seconds);
    // The game's sound: interleaved stereo s16 at `rate` Hz (48000; other rates are ignored). Any thread; queues only.
    void audio(uint32_t rate, const int16_t* frames, size_t count);
    // Finishes: the video's ffmpeg, then the mux with the sound into the output. Returns false on a failure; `report`
    // says what was written (frames, repeats, drops, seconds of video and sound) or what failed.
    bool stop(std::string& report);

private:
    struct Job {
        std::vector<uint8_t> pixels;
        uint64_t repeats = 1;
    };
    void writer();
    bool startVideo(uint32_t width, uint32_t height, int vkFormat, std::string& error);

    Options options_;
    FramePacer pacer_;
    std::mutex lock_;
    std::condition_variable ready_;
    std::deque<Job> queue_;
    std::vector<std::vector<uint8_t>> spare_;   // buffers the writer is done with
    std::vector<int16_t> sound_;                // samples waiting for the writer
    size_t maxQueued_ = 2;
    bool done_ = false;
    std::thread thread_;
    FILE* video_ = nullptr;   // ffmpeg's stdin
    FILE* soundFile_ = nullptr;
    std::string videoPath_, soundPath_, error_;
    std::atomic<bool> started_{false}, stopped_{false};
    double origin_ = 0.0;
    uint32_t width_ = 0, height_ = 0;
    // statistics (the render thread's, but `written`, `soundFrames` and `failed` the writer's, under lock_)
    uint64_t presented_ = 0, skipped_ = 0, dropped_ = 0, resized_ = 0, repeated_ = 0;
    uint64_t written_ = 0, soundFrames_ = 0;
    double lastSeconds_ = 0.0;
};

}  // namespace gt4
