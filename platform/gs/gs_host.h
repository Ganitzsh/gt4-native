#pragma once
// The PS2 graphics host: the game link (or a capture replay) feeding the graphics stream (VIF1, VU1, GIF
// paths, privileged registers) into the native renderer's front end on a worker thread, and its frame lists
// handed to the renderer. See docs/GS_BACKEND.md and docs/NATIVE_RENDERER.md.
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
        std::string texturePack;             // a texture pack's folder (texture_pack.h); empty: none
        bool hdTextures = true;              // its replacements on at start (setHdTextures)
        // The game's sound (stereo s16 frames at `rate` Hz), called on the worker thread as it arrives.
        std::function<void(uint32_t rate, const int16_t* frames, size_t count)> audio;
    };
    // Starts the game or opens the replay.
    static std::unique_ptr<GsHost> create(const Options& options, std::string& error);
    ~GsHost();
    GsHost(const GsHost&) = delete;
    GsHost& operator=(const GsHost&) = delete;

    // Lockstep replay: runs the records of the next VBlank. False at the end.
    bool advance();
    // The frame lists made since the last call, in the game's order (moved into `lists`); false if none.
    // The renderer draws every one of them (their draws make what later frames and reads see).
    bool takeNativeFrames(std::vector<gsn::FrameList>& lists);
    // A GS -> host read of drawn pixels the game waits for (live games): the render thread takes it after
    // the frame lists (takeNativeFrames, which then include the list cut at the read), draws them, reads the
    // rectangle back and answers with completeRead (ok = false: the game gets the memory model's bytes).
    bool takeReadRequest(gsn::ReadRequest& request);
    void completeRead(bool ok, std::vector<uint8_t>&& pixels);
    // Host -> game pad state (libpad2 layout, 18 bytes). Ignored without a live game.
    void sendPad(uint32_t port, bool connected, const uint8_t data[18]);
    // Stops or restarts the game's clock (its VBlanks, alarms and timers): the in-game settings menu.
    void setPaused(bool paused);
    // The texture pack's replacements on or off (from the next frame; nothing without a pack).
    void setHdTextures(bool on);
    // A gameplay option of the game (runtime.h GameOption): sent when it differs from the last value sent.
    void setGameOption(uint32_t id, uint32_t value);

    bool live() const;          // linked to a running game
    uint64_t vblanks() const;   // VBlanks received from the game or the capture (the game's clock)
    bool finished() const;      // the game exited, or the replay reached its end (without loop)
    int gameExitCode() const;   // the game's exit code once it has exited (0 otherwise)
    std::string status() const;     // one line for the overlay
    std::string summary() const;    // statistics for the log at exit

    struct Impl;
private:
    GsHost();
    std::unique_ptr<Impl> impl;
};

// The calling thread's CPU time so far (user and kernel), milliseconds; 0 where not known.
double threadCpuMilliseconds();

}  // namespace gt4::gs
