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
int GameLink::exitCode() const { return impl->exitCode; }
uint32_t GameLink::gameState() const { return impl->header->game_state; }
uint64_t GameLink::bytesReceived() const { return impl->received; }

std::unique_ptr<ReplaySource> ReplaySource::open(const std::string& path, std::string& error) {
    auto source = std::make_unique<ReplaySource>();
    source->file = path;
    size_t bytes = 0;
#ifdef _WIN32
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    LARGE_INTEGER length{};
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &length)) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        error = "cannot open the capture " + path;
        return nullptr;
    }
    bytes = static_cast<size_t>(length.QuadPart);
    if (bytes) {
        HANDLE section = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        void* view = section ? MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (section) CloseHandle(section);
        if (!view) {
            CloseHandle(file);
            error = "cannot map the capture " + path;
            return nullptr;
        }
        source->mapping = std::shared_ptr<void>(view, [](void* v) { UnmapViewOfFile(v); });
    }
    CloseHandle(file);
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    struct stat st {};
    if (fd < 0 || fstat(fd, &st) != 0) {
        if (fd >= 0) ::close(fd);
        error = "cannot open the capture " + path;
        return nullptr;
    }
    bytes = static_cast<size_t>(st.st_size);
    if (bytes) {
        void* view = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (view == MAP_FAILED) {
            ::close(fd);
            error = "cannot map the capture " + path;
            return nullptr;
        }
        source->mapping = std::shared_ptr<void>(view, [bytes](void* v) { munmap(v, bytes); });
    }
    ::close(fd);
#endif
    source->data = static_cast<const uint8_t*>(source->mapping.get());
    size_t used = 0;
    if (source->data)
        used = splitRecords(source->data, bytes, [&](const Gt4LinkRecord& r, const uint8_t*) {
            if (r.type == GT4_REC_VSYNC) ++source->vsyncCount;
        });
    if (used != bytes)
        std::fprintf(stderr, "Replay: %s ends with %zu bytes of an incomplete record (ignored)\n", path.c_str(), bytes - used);
    source->size = used;
    return source;
}

bool ReplaySource::nextFrame(const RecordCallback& onRecord) {
    while (offset + sizeof(Gt4LinkRecord) <= size) {
        Gt4LinkRecord header;
        std::memcpy(&header, data + offset, sizeof(header));
        const uint8_t* payload = data + offset + sizeof(header);
        offset += recordSpan(header.size);
        onRecord(header, payload);
        if (header.type == GT4_REC_VSYNC) return true;
    }
    return false;
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
