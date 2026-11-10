#pragma once
// PCSX2 GS dumps (PLAN.md C12): a frame of the game's GS input written as a `.gs` file that PCSX2's GS dump player
// renders in its software renderer, and read back. No GPU. The format is PCSX2's (pcsx2/GS/GSDump.{h,cpp}, GS state
// version 9, GSState::Freeze), checked against a dump PCSX2 wrote of GT4 (build/refs/night/gsdump_k001.gs.zst):
//
//   u32 0xFFFFFFFF, u32 header size, the header (9 x u32: state version, state size, serial offset and size, CRC,
//   screenshot width, height, offset and size; offsets from the header's start), the serial, the screenshot's pixels,
//   the state (state size bytes), the privileged registers (8 KiB, 0x12000000 on), then packets:
//     0 path size data   a transfer: path 1, 2 or 3 (PCSX2's Path1New, Path2, Path3), u32 size, the GIF data
//     1 field            a VSync (PCSX2 writes the registers packet just before each)
//     2 qwc              a GS -> host read of qwc quadwords (u32)
//     3 regs             the privileged registers, 8 KiB
//
// The state: u32 version, the environment registers (PRIM, PRMODECONT, TEXCLUT, SCANMSK, TEXA, FOGCOL, DIMX, DTHE,
// COLCLAMP, PABE, BITBLTBUF, TRXDIR, TRXPOS, TRXREG and TRXREG again) and each context's (XYOFFSET, TEX0, TEX1, CLAMP,
// MIPTBP1, MIPTBP2, SCISSOR, ALPHA, TEST, FBA, FRAME, ZBUF) as 64-bit values; the vertex being built (PCSX2's
// GSVertex: RGBAQ and ST 64-bit, UV and FOG 32-bit, XYZ 64-bit); 8 unused bytes; the image transfer in progress
// (x, y, w, h, its BITBLTBUF, TRXPOS and TRXREG, the rectangle, total, start and end bytes, and a write flag): 425
// bytes to here. Then the 4 MiB of GS memory in address order, each GIF path's tag and register index (4 x 20
// bytes) and Q (a float): 4 MiB + 509 bytes.
//
// A dump holds GS memory, which is game content: it stays on the owner's machines, like a capture.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gt4::gs {

struct GsDumpState {
    static constexpr uint32_t kVersion = 9;
    static constexpr size_t kRegisterBytes = 425;            // the state's part before GS memory
    static constexpr size_t kMemoryBytes = size_t(4) << 20;
    static constexpr size_t kBytes = kRegisterBytes + kMemoryBytes + 84;
    static constexpr size_t kPrivilegedBytes = 8192;

    // The environment registers, as their GIF A+D values.
    uint64_t prim = 0, prmodecont = 1, texclut = 0, scanmsk = 0, texa = 0, fogcol = 0, dimx = 0, dthe = 0,
             colclamp = 0, pabe = 0, bitbltbuf = 0, trxdir = 3, trxpos = 0, trxreg = 0;
    struct Context {
        uint64_t xyoffset = 0, tex0 = 0, tex1 = 0, clamp = 0, miptbp1 = 0, miptbp2 = 0, scissor = 0, alpha = 0,
                 test = 0, fba = 0, frame = 0, zbuf = 0;
        bool operator==(const Context&) const = default;
    };
    std::array<Context, 2> context{};
    // The vertex registers (XYZ: the last XYZ2/3 value; UV: U and V in 12.4, as the register's low 32 bits).
    uint64_t rgbaq = uint64_t(0x3F800000) << 32, st = 0, xyz = 0;
    uint32_t uv = 0, fog = 0;
    float q = 1.0f;   // the last ST's Q
    // The 8 unused bytes and the image transfer in progress (none), and every GIF path idle: what a dump at a frame's
    // start has. (The bytes are kept as read, so a dump read and written again is the same.)
    std::array<uint8_t, 77> transfer{};
    std::array<uint8_t, 80> paths{};
    std::vector<uint8_t> memory = std::vector<uint8_t>(kMemoryBytes, 0);   // GS memory, byte 0 = address 0

    std::vector<uint8_t> encode() const;   // kBytes
    // false (and `error`) when `bytes` is not a version-9 state of kBytes.
    bool decode(const uint8_t* bytes, size_t size, std::string& error);
    // The registers as one PACKED A+D GIF packet (EOP set; PRMODE given PRIM's attributes, as AC = 0 reads them
    // there): what makes another GS's state this one's, short of GS memory, the transfer in progress and Q. TRXDIR
    // is left out (writing it starts a transfer); XYZ too (a vertex kick). A TEX0 with CLD 1-5 loads the CLUT from GS
    // memory as it lands; the state does not carry the CLUT buffer, so a palette at CBP that changed after the PS2's
    // last load reads differently in the first draws (lane D: where to look first if the player's comparison differs
    // on paletted textures early in a frame).
    std::vector<uint64_t> registersAsGif() const;
};

class GsDumpWriter {
public:
    GsDumpWriter() = default;
    GsDumpWriter(const GsDumpWriter&) = delete;
    GsDumpWriter& operator=(const GsDumpWriter&) = delete;
    ~GsDumpWriter();

    // Starts the file: the header (the serial and CRC PCSX2 shows; no screenshot), the state and the privileged
    // registers at the start (kPrivilegedBytes).
    bool open(const std::string& path, const GsDumpState& state, const uint8_t* privileged, std::string& error,
              const std::string& serial = "SCUS-97328", uint32_t crc = 0x77E61C8A);
    void transfer(uint32_t path, const uint8_t* data, size_t bytes);   // path 1, 2 or 3; nothing for 0 bytes
    void readFifo(uint32_t qwc);
    // The end of a field, as PCSX2 writes it: the privileged registers, then the VSync (field 0 or 1).
    void vsync(uint32_t field, const uint8_t* privileged);
    bool close(std::string& error);   // false when a write failed
    bool isOpen() const { return file != nullptr; }

private:
    void put(const void* data, size_t bytes);
    std::FILE* file = nullptr;
    bool failed = false;
};

// A dump read back whole (an uncompressed `.gs`): what the round trip and tools/pcsx2_gsdump.py's checks read.
struct GsDump {
    struct Packet {
        uint8_t id = 0;     // 0 transfer, 1 VSync, 2 GS -> host read, 3 privileged registers
        uint8_t path = 0;   // a transfer's path; a VSync's field
        uint32_t qwc = 0;   // a read's quadwords
        std::vector<uint8_t> data;   // a transfer's bytes; the registers
    };
    std::string serial;
    uint32_t crc = 0;
    GsDumpState state;
    std::vector<uint8_t> privileged;
    std::vector<Packet> packets;
    bool read(const std::string& path, std::string& error);
};

}  // namespace gt4::gs
