#pragma once
// The host side of the game link (include/gt4/link/protocol.h) and the capture replay.
//
// GameLink creates the shared section "Local\gt4_link_<host pid>" (GT4_LINK_SIZE bytes), fills the
// header, starts gt4_game.exe with --link <name> [game arguments], copies the game -> host records out
// of their ring and writes host -> game records into the other one. A monitor thread logs the game's
// exit code (crashes included). Windows only; elsewhere start() reports that the link is unavailable.
//
// ReplaySource reads a capture file (the game's --capture output: the same records, concatenated) one
// VBlank at a time and hands its records out (captures run to many gigabytes: only a frame is held).
#include "gt4/link/protocol.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gt4::ps2 {

using RecordCallback = std::function<void(const Gt4LinkRecord&, const uint8_t*)>;

class GameLink {
public:
    struct Options {
        std::string gamePath;              // gt4_game.exe
        std::vector<std::string> gameArgs; // extra arguments after --link <name>
        std::string workingDirectory;      // empty: the host's
    };
    // Creates the section and starts the game. Null (and the reason) on failure.
    static std::unique_ptr<GameLink> start(const Options& options, std::string& error);
    ~GameLink();   // asks the game to quit (GT4_REC_HOST_CMD 1), waits up to 3 s, then terminates it
    GameLink(const GameLink&) = delete;
    GameLink& operator=(const GameLink&) = delete;

    // Copies out every complete game -> host record that is available (up to about `budget` bytes) and
    // calls `onRecord` for each. Returns the number of records.
    size_t poll(const RecordCallback& onRecord, size_t budget = 64u << 20);
    // Writes one host -> game record (thread-safe). Returns false if the ring stayed full for 100 ms.
    bool write(uint32_t type, const void* payload, uint32_t size);

    const std::string& name() const;
    bool running() const;                 // the game process has not exited
    int exitCode() const;                 // valid once !running()
    double processSeconds() const;        // the game process's CPU time so far (user and kernel, every thread)
    uint32_t gameState() const;           // header game_state (0 starting, 1 running, 2 exited)
    void setHostFrameDrawn(uint32_t vblank);              // header host_frame_drawn
    void gameHostWaits(uint32_t& count, uint32_t& ms) const;   // header game_host_waits, game_host_wait_ms
    uint64_t bytesReceived() const;

    struct Impl;
private:
    explicit GameLink(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};

class CaptureReader;   // (link_host.cpp)

class ReplaySource {
public:
    static std::unique_ptr<ReplaySource> open(const std::string& path, std::string& error);
    // Calls `onRecord` for each record up to and including the next VSYNC. Returns false at the end of
    // the file (no VSYNC found; the records before the end were still delivered). A payload stays valid until the
    // next call.
    bool nextFrame(const RecordCallback& onRecord);
    void rewind() { offset = 0; }
    size_t frames() const { return vsyncCount; }   // VSYNC records in the file
    const std::string& path() const { return file; }

private:
    // The capture is read one frame at a time, not mapped whole: a recording from boot runs to tens of gigabytes,
    // and a mapped one stayed in the process's memory as it was read (two replays at once of a 14.5 GB capture
    // left the machine short of memory). On Windows it bypasses the system's file cache too (link_host.cpp,
    // CaptureReader): through the cache, a replay took the machine's free memory to nothing while it ran.
    std::string file;
    std::shared_ptr<CaptureReader> in;
    uint64_t size = 0;                 // the bytes of its whole records
    uint64_t offset = 0;               // where the next frame starts
    size_t vsyncCount = 0;
    std::vector<uint8_t> frame;        // the records of the frame last delivered
};

// Writes records in the capture format (for tests and tools).
class CaptureWriter {
public:
    explicit CaptureWriter(const std::string& path);
    ~CaptureWriter();
    bool ok() const { return out != nullptr; }
    void write(uint32_t type, const void* payload, uint32_t size, const void* extra = nullptr, uint32_t extraSize = 0);
    void flush() { if (out) std::fflush(out); }
private:
    std::FILE* out = nullptr;
    uint64_t seq = 0;
};

}  // namespace gt4::ps2
