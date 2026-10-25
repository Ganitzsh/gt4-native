#pragma once
// The GS back end: paraLLEl-GS (LGPL-3.0-or-later, Arntzen Software AS; Vulkan compute) on its own
// Vulkan device, behind an interface that does not expose Granite or paraLLEl-GS headers.
//
// It takes what the game's DMA would have delivered to the GS: GIF packets per path (PATH1 from VU1
// XGKICK, PATH2 from VIF1 DIRECT, PATH3 from the GIF DMA channel), privileged register writes
// (PMODE ... BGCOLOR, CSR, IMR), and VSYNC. At each VSYNC it reads out the displayed frame the way the
// CRTC does (both read circuits, DISPFB/DISPLAY, background colour, interlace) and copies it to the
// CPU. SIGNAL, FINISH and LABEL writes are reported through the event sink; GS -> EE image transfers
// (TRXDIR = 1) are read with readTransfer(). See docs/GS_BACKEND.md.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt4::gs {

// One displayed frame, as the CRTC scanned it out.
struct DisplayFrame {
    uint32_t width = 0, height = 0;      // pixels in rgba
    std::vector<uint32_t> rgba;          // R8G8B8A8 (R in the low byte), top row first; A forced to 255
    uint32_t field = 0;                  // CSR.FIELD of this VBlank
    uint64_t vblank = 0;                 // the game's VBlank counter
    bool interlaced = false;
    uint32_t modeWidth = 0, modeHeight = 0;  // the video mode the frame fills (aspect-ratio reference)
};

enum class GsEventKind : uint32_t { Finish = 0, Signal = 1, Label = 2 };

struct BackendOptions {
    std::string gpu;              // "" or "auto": discrete GPU preferred; else an index or part of the name
    uint32_t superSampling = 1;   // 1 (faithful), 2, 4, 8, 16: paraLLEl-GS super-sampling
    bool highResolutionScanout = false;  // with >= 4x super-sampling: scan out at twice the resolution
    bool timestamps = false;      // GPU timestamps per stage (cost in the log)
};

class Backend {
public:
    using EventSink = std::function<void(GsEventKind kind, uint64_t payload)>;

    // Creates the Vulkan device and the GS. Returns null (and the reason) when no device qualifies.
    // Creating it reloads volk's global function pointers for its own instance: a caller that also uses
    // the volk globals must reload its own afterwards (volkLoadInstanceOnly + volkLoadDevice).
    static std::unique_ptr<Backend> create(const BackendOptions& options, std::string& error);
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    // GIF data of one path (1, 2 or 3); `bytes` is a multiple of 16. Path state persists across calls,
    // so a packet may be split over several calls.
    void gif(uint32_t path, const void* data, size_t bytes);
    // Privileged register: offset from 0x12000000 (0x0000 PMODE ... 0x00E0 BGCOLOR, 0x1000 CSR,
    // 0x1010 IMR, 0x1040 BUSDIR, 0x1080 SIGLBLID).
    void writePrivileged(uint32_t offset, uint64_t value);
    uint64_t readPrivileged(uint32_t offset) const;
    // End of a field: flushes the GS work and scans the display out into `frame` (CPU copy).
    // Returns false when nothing is displayed (both circuits off).
    bool vsync(uint32_t field, uint64_t vblank, DisplayFrame& frame);
    // GS -> EE transfer data after a TRXDIR write with XDIR = 1 (reads past the end give zeros).
    void readTransfer(void* out, uint32_t qwc);
    // Direct local memory access (4 MiB), for tests and debugging.
    void writeVram(uint32_t offset, const void* data, size_t bytes);
    void readVram(uint32_t offset, void* data, size_t bytes);
    void setEventSink(EventSink sink);
    void setSuperSampling(uint32_t rate, bool highResolutionScanout);

    std::string deviceName() const;
    // Accumulated GPU milliseconds per stage since creation (with timestamps on), for the log.
    std::string statistics() const;

private:
    Backend();
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace gt4::gs
