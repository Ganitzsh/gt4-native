#pragma once
// The host side of the game link (include/gt4/link/protocol.h) and the capture replay.
//
// GameLink creates the shared section "Local\gt4_link_<host pid>" (GT4_LINK_SIZE bytes), fills the
// header, starts gt4_game.exe with --link <name> [game arguments], copies the game -> host records out
// of their ring and writes host -> game records into the other one. A monitor thread logs the game's
// exit code (crashes included). Windows only; elsewhere start() reports that the link is unavailable.
//
// ReplaySource maps a capture file (the game's --capture output: the same records, concatenated) into
// memory, read-only (captures run to many gigabytes: nothing is copied), and hands the records out
// one VBlank at a time.
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
    uint32_t gameState() const;           // header game_state (0 starting, 1 running, 2 exited)
    uint64_t bytesReceived() const;

    struct Impl;
private:
    explicit GameLink(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};

class ReplaySource {
public:
    static std::unique_ptr<ReplaySource> open(const std::string& path, std::string& error);
    // Calls `onRecord` for each record up to and including the next VSYNC. Returns false at the end of
    // the file (no VSYNC found; the records before the end were still delivered).
    bool nextFrame(const RecordCallback& onRecord);
    void rewind() { offset = 0; }
    size_t frames() const { return vsyncCount; }   // VSYNC records in the file
    const std::string& path() const { return file; }

private:
    std::string file;
    const uint8_t* data = nullptr;   // the mapped capture
    size_t size = 0;                 // its whole records
    std::shared_ptr<void> mapping;   // unmaps the file with the last owner
    size_t offset = 0;
    size_t vsyncCount = 0;
};

// Writes records in the capture format (for tests and tools).
class CaptureWriter {
public:
    explicit CaptureWriter(const std::string& path);
    ~CaptureWriter();
    bool ok() const { return out != nullptr; }
    void write(uint32_t type, const void* payload, uint32_t size, const void* extra = nullptr, uint32_t extraSize = 0);
private:
    std::FILE* out = nullptr;
    uint64_t seq = 0;
};

}  // namespace gt4::ps2
