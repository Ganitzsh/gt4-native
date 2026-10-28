#pragma once
// The PS2 graphics host: the game link (or a capture replay) feeding the graphics stream (VIF1, VU1,
// GIF paths, privileged registers) into the GS back end on a worker thread, and the displayed frames
// handed to the renderer. See docs/GS_BACKEND.md.
#include "gs_backend.h"
#include "gs_frontend.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt4::gs {

class GsHost {
public:
    struct Options {
        std::string gamePath;                // --game: start gt4_game.exe and link to it
        std::vector<std::string> gameArgs;   // --game-args
        std::string replayPath;              // --replay: a capture file instead of a live game
        bool lockstep = false;               // replay: one VBlank per host frame (deterministic captures)
        bool loop = false;                   // replay: start again at the end
        BackendOptions backend;
        bool nativeRenderer = false;         // also build the native renderer's frame lists (takeNativeFrame)
        // The game's sound (stereo s16 frames at `rate` Hz), called on the worker thread as it arrives.
        std::function<void(uint32_t rate, const int16_t* frames, size_t count)> audio;
    };
    // Creates the GS back end (its own Vulkan device: the caller must reload its volk function pointers
    // afterwards, see Backend::create) and starts the game or opens the replay.
    static std::unique_ptr<GsHost> create(const Options& options, std::string& error);
    ~GsHost();
    GsHost(const GsHost&) = delete;
    GsHost& operator=(const GsHost&) = delete;

    // Lockstep replay: runs the records of the next VBlank and waits for its frame. False at the end.
    bool advance();
    // The newest displayed frame when it is newer than `serial` (which is updated).
    bool takeFrame(DisplayFrame& frame, uint64_t& serial);
    // The newest native frame list when it is newer than `serial` (swapped into `list`).
    bool takeNativeFrame(gsn::FrameList& list, uint64_t& serial);
    // Whether native frame lists are wanted (Options::nativeRenderer): while not, the front end keeps
    // following the stream but its frames are dropped.
    void setNativeFrames(bool wanted);
    // Host -> game pad state (libpad2 layout, 18 bytes). Ignored without a live game.
    void sendPad(uint32_t port, bool connected, const uint8_t data[18]);
    // Stops or restarts the game's clock (its VBlanks, alarms and timers): the in-game settings menu.
    void setPaused(bool paused);
    // A gameplay option of the game (runtime.h GameOption): sent when it differs from the last value sent.
    void setGameOption(uint32_t id, uint32_t value);

    bool live() const;          // linked to a running game
    bool finished() const;      // the game exited, or the replay reached its end (without loop)
    int gameExitCode() const;   // the game's exit code once it has exited (0 otherwise)
    std::string status() const;     // one line for the overlay
    std::string summary() const;    // statistics for the log at exit
    std::string deviceName() const;

    struct Impl;
private:
    GsHost();
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4::gs
