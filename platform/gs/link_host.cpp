// Host side of the game link and capture replay (see link_host.h).
#include "link_host.h"

#include "stream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gt4::ps2 {

namespace {

void ringRead(const uint8_t* ring, uint32_t size, uint64_t position, void* destination, size_t n) {
    const uint32_t offset = static_cast<uint32_t>(position % size);
    const size_t first = std::min<size_t>(n, size - offset);
    std::memcpy(destination, ring + offset, first);
    if (n > first) std::memcpy(static_cast<uint8_t*>(destination) + first, ring, n - first);
}

void ringWrite(uint8_t* ring, uint32_t size, uint64_t position, const void* source, size_t n) {
    const uint32_t offset = static_cast<uint32_t>(position % size);
    const size_t first = std::min<size_t>(n, size - offset);
    std::memcpy(ring + offset, source, first);
    if (n > first) std::memcpy(ring, static_cast<const uint8_t*>(source) + first, n - first);
}

#ifdef _WIN32
// One argument quoted for CreateProcess (the MSVC runtime's parsing rules).
std::string quoteArgument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\"") == std::string::npos) return argument;
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : argument) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') out.append(backslashes * 2 + 1, '\\');
        else out.append(backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');
    return out + "\"";
}

const char* exitDescription(DWORD code) {
    switch (code) {
    case 0xC0000005: return "access violation";
    case 0xC000001D: return "illegal instruction";
    case 0xC0000094: return "integer division by zero";
    case 0xC00000FD: return "stack overflow";
    case 0xC0000409: return "stack buffer overrun / fail-fast";
    case 0xC0000374: return "heap corruption";
    case 0x80000003: return "breakpoint";
    case 0x40010004: return "terminated by the host";
    default: return code >= 0xC0000000 ? "crash" : nullptr;
    }
}
#endif

}  // namespace

struct GameLink::Impl {
    std::string name;
#ifdef _WIN32
    HANDLE mapping = nullptr;
    HANDLE process = nullptr;
#endif
    uint8_t* view = nullptr;
    Gt4LinkHeader* header = nullptr;
    uint8_t* g2h = nullptr;
    uint8_t* h2g = nullptr;
    std::mutex writeMutex;
    std::thread monitor;
    std::atomic<bool> running{false};
    std::atomic<int> exitCode{0};
    std::atomic<uint64_t> received{0};
    uint64_t sequence = 0;
    std::vector<uint8_t> batch;

    ~Impl() {
#ifdef _WIN32
        if (monitor.joinable()) monitor.join();
        if (process) CloseHandle(process);
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
#endif
    }
};

GameLink::GameLink(std::unique_ptr<Impl> i) : impl(std::move(i)) {}

std::unique_ptr<GameLink> GameLink::start(const Options& options, std::string& error) {
#ifndef _WIN32
    (void)options;
    error = "the game link is implemented for Windows only (use --replay on this platform)";
    return nullptr;
#else
    auto impl = std::make_unique<Impl>();
    impl->name = "Local\\gt4_link_" + std::to_string(GetCurrentProcessId());
    impl->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, GT4_LINK_SIZE, impl->name.c_str());
    if (!impl->mapping) {
        error = "CreateFileMapping(" + impl->name + ") failed: Windows error " + std::to_string(GetLastError());
        return nullptr;
    }
    impl->view = static_cast<uint8_t*>(MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, GT4_LINK_SIZE));
    if (!impl->view) {
        error = "MapViewOfFile failed: Windows error " + std::to_string(GetLastError());
        return nullptr;
    }
    std::memset(impl->view, 0, 4096);
    auto* header = reinterpret_cast<Gt4LinkHeader*>(impl->view);
    header->magic = GT4_LINK_MAGIC;
    header->version = GT4_LINK_VERSION;
    header->g2h_offset = 4096;
    header->g2h_size = GT4_LINK_G2H_SIZE;
    header->h2g_offset = 4096 + GT4_LINK_G2H_SIZE;
    header->h2g_size = GT4_LINK_H2G_SIZE;
    header->host_pid = GetCurrentProcessId();
    header->game_state = 0;
    impl->header = header;
    impl->g2h = impl->view + header->g2h_offset;
    impl->h2g = impl->view + header->h2g_offset;

    std::string commandLine = quoteArgument(options.gamePath) + " --link " + quoteArgument(impl->name);
    for (const auto& argument : options.gameArgs) commandLine += " " + quoteArgument(argument);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::vector<char> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back('\0');
    if (!CreateProcessA(options.gamePath.c_str(), mutableLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        options.workingDirectory.empty() ? nullptr : options.workingDirectory.c_str(), &startup, &info)) {
        error = "cannot start " + options.gamePath + ": Windows error " + std::to_string(GetLastError());
        return nullptr;
    }
    CloseHandle(info.hThread);
    impl->process = info.hProcess;
    impl->running = true;
    std::printf("Link: started %s (pid %lu) with --link %s\n", options.gamePath.c_str(), info.dwProcessId, impl->name.c_str());
    Impl* raw = impl.get();
    impl->monitor = std::thread([raw] {
        WaitForSingleObject(raw->process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(raw->process, &code);
        raw->exitCode = static_cast<int>(code);
        raw->header->game_state = 2;
        raw->running = false;
        const char* what = exitDescription(code);
        std::printf("Link: gt4_game exited with code %ld (0x%08lX)%s%s\n", static_cast<long>(static_cast<int32_t>(code)),
                    static_cast<unsigned long>(code), what ? ": " : "", what ? what : "");
        std::fflush(stdout);
    });
    return std::unique_ptr<GameLink>(new GameLink(std::move(impl)));
#endif
}

GameLink::~GameLink() {
#ifdef _WIN32
    if (impl && impl->running) {
        const uint32_t quit = 1;
        write(GT4_REC_HOST_CMD, &quit, sizeof(quit));
        if (WaitForSingleObject(impl->process, 3000) != WAIT_OBJECT_0) {
            std::printf("Link: gt4_game did not quit; terminating it\n");
            TerminateProcess(impl->process, 0x40010004);
        }
    }
#endif
}

size_t GameLink::poll(const RecordCallback& onRecord, size_t budget) {
    Gt4LinkHeader* header = impl->header;
    const uint32_t size = header->g2h_size;
    const uint64_t head = header->g2h.head;
    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t tail = header->g2h.tail;
    if (head <= tail) return 0;
    // Copy whole records out of the ring first, then release the space, then dispatch.
    uint64_t end = tail;
    while (end + sizeof(Gt4LinkRecord) <= head && end - tail < budget) {
        Gt4LinkRecord record;
        ringRead(impl->g2h, size, end, &record, sizeof(record));
        const size_t span = recordSpan(record.size);
        if (end + span > head) break;
        end += span;
    }
    if (end == tail) return 0;
    impl->batch.resize(static_cast<size_t>(end - tail));
    ringRead(impl->g2h, size, tail, impl->batch.data(), impl->batch.size());
    std::atomic_thread_fence(std::memory_order_release);
    header->g2h.tail = end;
    impl->received += end - tail;
    size_t records = 0;
    splitRecords(impl->batch.data(), impl->batch.size(), [&](const Gt4LinkRecord& r, const uint8_t* p) {
        ++records;
        onRecord(r, p);
    });
    return records;
}

bool GameLink::write(uint32_t type, const void* payload, uint32_t payloadSize) {
    std::lock_guard<std::mutex> lock(impl->writeMutex);
    Gt4LinkHeader* header = impl->header;
    const uint32_t size = header->h2g_size;
    const size_t span = recordSpan(payloadSize);
    if (span > size / 2) return false;
    uint64_t head = header->h2g.head;
    for (int waited = 0; head + span - header->h2g.tail > size; ++waited) {
        if (waited > 100 || !impl->running) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Gt4LinkRecord record{type, payloadSize, ++impl->sequence};
    static const uint8_t zeros[16] = {};
    ringWrite(impl->h2g, size, head, &record, sizeof(record));
    if (payloadSize) ringWrite(impl->h2g, size, head + sizeof(record), payload, payloadSize);
    const size_t padding = span - sizeof(record) - payloadSize;
    if (padding) ringWrite(impl->h2g, size, head + sizeof(record) + payloadSize, zeros, padding);
    std::atomic_thread_fence(std::memory_order_release);
    header->h2g.head = head + span;
    return true;
}

const std::string& GameLink::name() const { return impl->name; }
bool GameLink::running() const { return impl->running; }
double GameLink::processSeconds() const {
    // what the game reports of itself (a game that predates that: the process's times read from here)
    if (impl->header && impl->header->game_cpu_ms) return impl->header->game_cpu_ms / 1000.0;
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!impl->process || !GetProcessTimes(impl->process, &created, &exited, &kernel, &user)) return 0.0;
    auto seconds = [](const FILETIME& t) { return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7; };
    return seconds(kernel) + seconds(user);
#else
    return 0.0;   // (no game process here: GameLink::start is Windows-only until the POSIX link, PLAN.md C9)
#endif
}

int GameLink::exitCode() const { return impl->exitCode; }
uint32_t GameLink::gameState() const { return impl->header->game_state; }
void GameLink::setHostFrameDrawn(uint32_t vblank) {
    if (impl->header && vblank) impl->header->host_frame_drawn = vblank;
}
void GameLink::gameHostWaits(uint32_t& count, uint32_t& ms) const {
    count = impl->header ? impl->header->game_host_waits : 0;
    ms = impl->header ? impl->header->game_host_wait_ms : 0;
}
uint64_t GameLink::bytesReceived() const { return impl->received; }

namespace {

bool seekTo(std::FILE* f, uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<__int64>(at), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
}

uint64_t fileSize(std::FILE* f) {
    std::fseek(f, 0, SEEK_END);
#ifdef _WIN32
    const int64_t n = _ftelli64(f);
#else
    const int64_t n = ftello(f);
#endif
    std::fseek(f, 0, SEEK_SET);
    return n > 0 ? static_cast<uint64_t>(n) : 0;
}

}  // namespace

std::unique_ptr<ReplaySource> ReplaySource::open(const std::string& path, std::string& error) {
    auto source = std::make_unique<ReplaySource>();
    source->file = path;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open the capture " + path;
        return nullptr;
    }
    source->in = std::shared_ptr<std::FILE>(f, [](std::FILE* p) { std::fclose(p); });
    std::setvbuf(f, nullptr, _IOFBF, 1 << 20);
    const uint64_t bytes = fileSize(f);
    // The VBlanks, from the records' headers, read in 4 MB chunks; a payload larger than what is left of a chunk is
    // skipped by seeking. The file goes through the system's cache once and nothing of it stays in this process.
    std::vector<uint8_t> chunk(4u << 20);
    uint64_t at = 0;            // the start of the next record in the file
    uint64_t chunkAt = 0;       // the file position of chunk[0]
    size_t have = 0;            // bytes in chunk
    while (at + sizeof(Gt4LinkRecord) <= bytes) {
        if (at < chunkAt || at + sizeof(Gt4LinkRecord) > chunkAt + have) {   // the header is not in the chunk
            if (!seekTo(f, at)) break;
            chunkAt = at;
            have = std::fread(chunk.data(), 1, chunk.size(), f);
            if (have < sizeof(Gt4LinkRecord)) break;
        }
        Gt4LinkRecord header;
        std::memcpy(&header, chunk.data() + (at - chunkAt), sizeof header);
        const uint64_t span = recordSpan(header.size);
        if (at + span > bytes) break;   // an incomplete record at the end
        at += span;
        source->size = at;
        if (header.type == GT4_REC_VSYNC) ++source->vsyncCount;
    }
    if (source->size != bytes)
        std::fprintf(stderr, "Replay: %s ends with %llu bytes of an incomplete record (ignored)\n", path.c_str(),
                     static_cast<unsigned long long>(bytes - source->size));
    return source;
}

bool ReplaySource::nextFrame(const RecordCallback& onRecord) {
    std::FILE* f = in.get();
    if (!f || offset + sizeof(Gt4LinkRecord) > size || !seekTo(f, offset)) return false;
    // The frame's records into one buffer first (their payloads stay valid until the next frame), then delivered.
    frame.clear();
    std::vector<size_t> starts;
    bool vsync = false;
    while (!vsync && offset + sizeof(Gt4LinkRecord) <= size) {
        Gt4LinkRecord header;
        if (std::fread(&header, sizeof header, 1, f) != 1) break;
        const uint64_t span = recordSpan(header.size);
        if (offset + span > size) break;
        const size_t at = frame.size();
        frame.resize(at + static_cast<size_t>(span));
        std::memcpy(frame.data() + at, &header, sizeof header);
        const size_t rest = static_cast<size_t>(span) - sizeof header;
        if (rest && std::fread(frame.data() + at + sizeof header, 1, rest, f) != rest) break;
        starts.push_back(at);
        offset += span;
        vsync = header.type == GT4_REC_VSYNC;
    }
    for (size_t at : starts) {
        Gt4LinkRecord header;
        std::memcpy(&header, frame.data() + at, sizeof header);
        onRecord(header, frame.data() + at + sizeof header);
    }
    return vsync;
}

CaptureWriter::CaptureWriter(const std::string& path) : out(std::fopen(path.c_str(), "wb")) {}
CaptureWriter::~CaptureWriter() { if (out) std::fclose(out); }

void CaptureWriter::write(uint32_t type, const void* payload, uint32_t size, const void* extra, uint32_t extraSize) {
    if (!out) return;
    const Gt4LinkRecord record{type, size + extraSize, ++seq};
    static const uint8_t zeros[16] = {};
    std::fwrite(&record, sizeof(record), 1, out);
    if (size) std::fwrite(payload, 1, size, out);
    if (extraSize) std::fwrite(extra, 1, extraSize, out);
    const size_t padding = recordSpan(size + extraSize) - sizeof(record) - size - extraSize;
    if (padding) std::fwrite(zeros, 1, padding, out);
}

}  // namespace gt4::ps2
