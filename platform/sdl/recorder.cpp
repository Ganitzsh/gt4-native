#include "recorder.h"

#include <volk.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace gt4 {
namespace {

constexpr size_t kQueueBytes = size_t(128) << 20;   // frames waiting for ffmpeg: beyond this a frame is dropped

FILE* openPipe(const std::string& command) {
#ifdef _WIN32
    return _popen(command.c_str(), "wb");
#else
    return popen(command.c_str(), "w");
#endif
}

int closePipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

// A path or executable as one shell word (cmd.exe on Windows, sh elsewhere).
std::string quote(const std::string& text) {
#ifdef _WIN32
    return "\"" + text + "\"";
#else
    std::string out = "'";
    for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
#endif
}

// cmd.exe /c strips the outer quotes of a line that starts with one: the whole line goes in one more pair.
std::string shellLine(const std::string& line) {
#ifdef _WIN32
    return "\"" + line + "\"";
#else
    return line;
#endif
}

const char* pixelFormat(int vkFormat) {
    switch (vkFormat) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB: return "bgra";
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: return "rgba";
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "x2bgr10le";
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "x2rgb10le";
        default: return nullptr;
    }
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Recorder::Recorder(Options options) : options_(std::move(options)), pacer_(options_.fps) {
    if (options_.videoArgs.empty()) options_.videoArgs = kDefaultVideoArgs;
    videoPath_ = options_.path + ".video.mkv";
    soundPath_ = options_.path + ".sound.raw";
}

Recorder::~Recorder() {
    std::string report;
    stop(report);
}

bool Recorder::startVideo(uint32_t width, uint32_t height, int vkFormat, std::string& error) {
    const char* format = pixelFormat(vkFormat);
    if (!format) {
        error = "the output format " + std::to_string(vkFormat) + " can't be recorded";
        return false;
    }
    soundFile_ = std::fopen(soundPath_.c_str(), "wb");
    if (!soundFile_) {
        error = "can't write " + soundPath_;
        return false;
    }
    char rate[32];
    std::snprintf(rate, sizeof rate, "%g", options_.fps);
    const std::string line = quote(options_.ffmpeg) + " -hide_banner -loglevel error -y -f rawvideo -pix_fmt " +
        format + " -s " + std::to_string(width) + "x" + std::to_string(height) + " -framerate " + rate + " -i - " +
        options_.videoArgs + " " + quote(videoPath_);
    video_ = openPipe(shellLine(line));
    if (!video_) {
        error = "can't start ffmpeg: " + line;
        return false;
    }
    // A frame is 8 MB at 1080p: written to the pipe in large blocks, not the C library's few kilobytes at a time.
    std::setvbuf(video_, nullptr, _IOFBF, size_t(8) << 20);
    width_ = width;
    height_ = height;
    maxQueued_ = std::max<size_t>(2, kQueueBytes / (size_t(width) * height * 4));
    thread_ = std::thread([this] { writer(); });
    return true;
}

void Recorder::frame(const uint8_t* pixels, uint32_t width, uint32_t height, int vkFormat, double seconds) {
    if (stopped_) return;
    if (!started_) {
        std::string error;
        if (!startVideo(width, height, vkFormat, error)) {
            std::fprintf(stderr, "Recording: %s; not recording\n", error.c_str());
            error_ = error;
            stopped_ = true;
            return;
        }
        origin_ = seconds;
        // The sound from now on, behind the silence of this frame's way from its present to here (the renderer hands
        // a frame over when the GPU is done with it, a frame or two later), so the two start together.
        const double late = steadySeconds() - seconds;
        const size_t silence = late > 0.0 && late < 1.0 ? static_cast<size_t>(late * 48000.0) : 0;
        {
            std::lock_guard<std::mutex> g(lock_);
            sound_.assign(silence * 2, 0);
        }
        started_ = true;
        std::fprintf(stderr, "Recording %ux%u at %.0f fps to %s\n", width, height, options_.fps,
                     options_.path.c_str());
    }
    ++presented_;
    if (width != width_ || height != height_) {   // ffmpeg takes one size: frames of another (a resize) are left out
        if (resized_++ == 0)
            std::fprintf(stderr, "Recording: the output is now %ux%u; such frames are left out\n", width, height);
        return;
    }
    lastSeconds_ = seconds - origin_;
    const uint64_t copies = pacer_.copies(lastSeconds_);
    if (copies == 0) {
        ++skipped_;
        return;
    }
    std::vector<uint8_t> buffer;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (!error_.empty() && done_) return;
        if (queue_.size() >= maxQueued_) {   // ffmpeg is behind: this frame's place in the video repeats the last
            queue_.back().repeats += copies;  // queued one, so the video keeps real time and stays with the sound
            dropped_ += copies;
            return;
        }
        if (!spare_.empty()) {
            buffer = std::move(spare_.back());
            spare_.pop_back();
        }
    }
    buffer.resize(size_t(width) * height * 4);
    std::memcpy(buffer.data(), pixels, buffer.size());
    repeated_ += copies - 1;
    {
        std::lock_guard<std::mutex> g(lock_);
        queue_.push_back(Job{std::move(buffer), copies});
    }
    ready_.notify_one();
}

void Recorder::audio(uint32_t rate, const int16_t* frames, size_t count) {
    if (!started_ || stopped_ || rate != 48000 || !count) return;
    std::lock_guard<std::mutex> g(lock_);
    sound_.insert(sound_.end(), frames, frames + count * 2);
}

void Recorder::writer() {
    std::vector<int16_t> sound;
    for (;;) {
        Job job;
        bool haveJob = false, finish = false;
        {
            std::unique_lock<std::mutex> g(lock_);
            ready_.wait_for(g, std::chrono::milliseconds(50), [this] { return done_ || !queue_.empty(); });
            if (!queue_.empty()) {
                job = std::move(queue_.front());
                queue_.pop_front();
                haveJob = true;
            }
            sound.swap(sound_);
            finish = done_ && queue_.empty() && !haveJob;
        }
        if (!sound.empty()) {
            const size_t frames = sound.size() / 2;
            if (std::fwrite(sound.data(), 4, frames, soundFile_) != frames) {
                std::lock_guard<std::mutex> g(lock_);
                if (error_.empty()) error_ = "writing the sound to " + soundPath_ + " failed";
            }
            std::lock_guard<std::mutex> g(lock_);
            soundFrames_ += frames;
            sound.clear();
        }
        if (haveJob) {
            bool ok = true;
            for (uint64_t k = 0; k < job.repeats && ok; ++k)
                ok = std::fwrite(job.pixels.data(), 1, job.pixels.size(), video_) == job.pixels.size();
            std::lock_guard<std::mutex> g(lock_);
            if (ok) {
                written_ += job.repeats;
            } else if (error_.empty()) {
                error_ = "ffmpeg stopped taking frames (see its messages above)";
                std::fprintf(stderr, "Recording: %s\n", error_.c_str());
            }
            spare_.push_back(std::move(job.pixels));
            if (!ok) {   // nothing more goes to a dead ffmpeg: what is queued is dropped
                for (Job& j : queue_) dropped_ += j.repeats;
                queue_.clear();
                done_ = true;
            }
        }
        if (finish) return;
    }
}

bool Recorder::stop(std::string& report) {
    if (stopped_.exchange(true)) {
        report = error_.empty() ? "" : "Recording failed: " + error_;
        return error_.empty();
    }
    if (!started_) {
        report = "Recording: no frame was presented; nothing written";
        return false;
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        done_ = true;
    }
    ready_.notify_all();
    if (thread_.joinable()) thread_.join();
    const int ffmpegExit = video_ ? closePipe(video_) : -1;
    video_ = nullptr;
    if (soundFile_) std::fclose(soundFile_);
    soundFile_ = nullptr;
    const double videoSeconds = double(written_) / options_.fps, soundSeconds = double(soundFrames_) / 48000.0;
    const std::string resized = resized_ ? ", " + std::to_string(resized_) + " of another size left out" : "";
    char stats[512];
    std::snprintf(stats, sizeof stats,
                  "%llu frames presented, %llu video frames written (%.2f s at %.0f fps): %llu repeats filling late "
                  "presents, %llu dropped (ffmpeg behind: the frame before shown again), %llu skipped (drawn faster "
                  "than %.0f fps)%s; sound %.2f s",
                  static_cast<unsigned long long>(presented_), static_cast<unsigned long long>(written_), videoSeconds,
                  options_.fps, static_cast<unsigned long long>(repeated_), static_cast<unsigned long long>(dropped_),
                  static_cast<unsigned long long>(skipped_), options_.fps,
                  resized.c_str(), soundSeconds);
    if (error_.empty() && ffmpegExit != 0) error_ = "ffmpeg exited with " + std::to_string(ffmpegExit);
    if (!error_.empty()) {
        report = "Recording failed: " + error_ + " (" + stats + "); kept " + videoPath_ + " and " + soundPath_;
        return false;
    }
    // The mux: the video as encoded, the sound encoded once here (AAC), into the container PATH names.
    std::string line = quote(options_.ffmpeg) + " -hide_banner -loglevel error -y -i " + quote(videoPath_);
    if (soundFrames_)
        line += " -f s16le -ar 48000 -ac 2 -i " + quote(soundPath_) + " -map 0:v -map 1:a -c:a aac -b:a 192k";
    line += " -c:v copy";
    const std::string ext = std::filesystem::path(options_.path).extension().string();
    if (ext == ".mp4" || ext == ".mov") line += " -movflags +faststart";
    line += " " + quote(options_.path);
    const int muxExit = std::system(shellLine(line).c_str());
    if (muxExit != 0) {
        report = "Recording failed: the mux exited with " + std::to_string(muxExit) + " (" + stats + "); kept " +
                 videoPath_ + " and " + soundPath_ + "; the command: " + line;
        return false;
    }
    std::error_code ignored;
    std::filesystem::remove(videoPath_, ignored);
    std::filesystem::remove(soundPath_, ignored);
    report = "Recorded " + options_.path + ": " + stats;
    return true;
}

}  // namespace gt4
