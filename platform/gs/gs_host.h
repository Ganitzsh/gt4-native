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
        bool texturePackSynchronous = false; // replacements decoded at first use, not on the pack's workers
                                             // (TexturePack::setSynchronous: gt4_host --fixed-step, replays and
                                             // sessions, the same frames every run; a live game keeps it off)
        // The game's sound (stereo s16 frames at `rate` Hz), called on the worker thread as it arrives.
        std::function<void(uint32_t rate, const int16_t* frames, size_t count)> audio;
        // The game's pad motors (GT4_REC_RUMBLE): port, small (0 / 1), large (0-255); called on the stream's thread.
        std::function<void(uint32_t port, uint32_t small, uint32_t large)> rumble;
        // The settings pages (docs/modern/MENU_DESIGN.md 3.6), called on the stream's thread with the record's
        // payload: a page's draw list (GT4_REC_UI) and its requests (GT4_REC_SETTINGS_SET); platform/ui/ui_records.h
        // decodes them.
        std::function<void(const uint8_t* payload, size_t size)> ui;
        std::function<void(const uint8_t* payload, size_t size)> settingsSet;
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
    // Waits up to `ms` milliseconds for a displayed frame list or a read request since the last call; true if
    // one came (the render thread presents only the game's frames: nothing to draw in between).
    bool waitNativeFrames(int ms);
    // A GS -> host read of drawn pixels the game waits for (live games): the render thread takes it after
    // the frame lists (takeNativeFrames, which then include the list cut at the read), draws them, reads the
    // rectangle back and answers with completeRead (ok = false: the game gets the memory model's bytes).
    bool takeReadRequest(gsn::ReadRequest& request);
    void completeRead(bool ok, std::vector<uint8_t>&& pixels);
    // Whether a read request waits to be taken (the render thread's pacing wait ends for it: the game waits meanwhile).
    bool readRequested() const;
    // An asynchronous read's pixels (gsn::FrameList::readbacks, packed as the GS keeps them) to the game, which did not
    // wait for them: GS_IMAGE `id`, `qwc` qwords.
    void answerRead(uint32_t id, uint32_t qwc, const std::vector<uint8_t>& pixels);
    // Requests the GS thread queued for the game (from the main thread, which owns the link's host -> game ring).
    void sendRequests();
    // Host -> game pad state (libpad2 layout, 18 bytes). Ignored without a live game.
    void sendPad(uint32_t port, bool connected, const uint8_t data[18]);
    // Stops or restarts the game's clock (its VBlanks, alarms and timers): the in-game settings menu.
    void setPaused(bool paused);
    // The settings pages: a GT4_REC_SETTINGS payload (schema, values, a request's result: ui_records.h), and host
    // command 6 (open the settings at the next safe point) or 7 (close them). Ignored without a live game.
    void sendSettings(const std::vector<uint8_t>& payload);
    void sendMenuCommand(uint32_t command);
    // The texture pack's replacements on or off (from the next frame; nothing without a pack).
    void setHdTextures(bool on);
    // A gameplay option of the game (runtime.h GameOption): sent when it differs from the last value sent.
    void setGameOption(uint32_t id, uint32_t value);

    bool live() const;          // linked to a running game
    uint64_t vblanks() const;   // VBlanks received from the game or the capture (the game's clock)
    uint64_t drawnFrames() const;   // frames the game drew (displayed frame lists), not VBlanks
    // The newest frame the renderer drew or skipped (its FrameList::vblank), for the game's last-resort wait
    // (protocol.h host_frame_drawn); and that wait's count and milliseconds so far.
    void setFrameDrawn(uint64_t vblank);
    void hostWaits(uint32_t& count, uint32_t& ms) const;
    bool finished() const;      // the game exited, or the replay reached its end (without loop)
    int gameExitCode() const;   // the game's exit code once it has exited (0 otherwise)
    double gameCpuSeconds() const;   // the game process's CPU time so far, every thread (0 without a live game)
    std::string status() const;     // one line for the overlay
    std::string summary() const;    // statistics for the log at exit

    struct Impl;
private:
    GsHost();
    std::unique_ptr<Impl> impl;
};

// The calling thread's CPU time so far (user and kernel), milliseconds; 0 where not known.
double threadCpuMilliseconds();
// This process's CPU time so far (every thread), milliseconds; 0 where not known.
double processCpuMilliseconds();

}  // namespace gt4::gs
