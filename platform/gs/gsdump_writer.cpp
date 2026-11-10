// gsdump_writer.h. The byte layout is PCSX2's (GSDump.cpp's AddHeader, Transfer, ReadFIFO and VSync; GSState.cpp's
// Freeze for state version 9), checked against a dump PCSX2 wrote: its register part decodes to GT4's values (FRAME
// 0x010A008C, SCISSOR 640 x 224, ZBUF Z24 at 0x1A2) and encodes back to the same bytes.
#include "gsdump_writer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

namespace gt4::gs {

namespace {

// The state's offsets (bytes from its start).
constexpr size_t kEnv = 4;                 // 15 x u64
constexpr size_t kContexts = kEnv + 15 * 8;   // 2 x 12 x u64
constexpr size_t kVertex = kContexts + 2 * 12 * 8;   // RGBAQ, ST (u64), UV, FOG (u32), XYZ (u64)
constexpr size_t kTransfer = kVertex + 32;   // 8 unused bytes, then the transfer in progress
static_assert(kTransfer + 77 == GsDumpState::kRegisterBytes, "PCSX2's state version 9: 425 bytes before GS memory");
constexpr size_t kPaths = GsDumpState::kRegisterBytes + GsDumpState::kMemoryBytes;
constexpr size_t kQ = kPaths + 80;
static_assert(kQ + 4 == GsDumpState::kBytes, "PCSX2's state version 9: 4 MiB + 509 bytes");

template <class T> void store(uint8_t* at, T value) { std::memcpy(at, &value, sizeof value); }
template <class T> T load(const uint8_t* at) {
    T value;
    std::memcpy(&value, at, sizeof value);
    return value;
}

// The environment's registers in the state's order (TRXREG a second time: an obsolete copy PCSX2 still writes).
uint64_t GsDumpState::* const kEnvOrder[] = {
    &GsDumpState::prim,      &GsDumpState::prmodecont, &GsDumpState::texclut,  &GsDumpState::scanmsk,
    &GsDumpState::texa,      &GsDumpState::fogcol,     &GsDumpState::dimx,     &GsDumpState::dthe,
    &GsDumpState::colclamp,  &GsDumpState::pabe,       &GsDumpState::bitbltbuf, &GsDumpState::trxdir,
    &GsDumpState::trxpos,    &GsDumpState::trxreg,     &GsDumpState::trxreg,
};
uint64_t GsDumpState::Context::* const kContextOrder[] = {
    &GsDumpState::Context::xyoffset, &GsDumpState::Context::tex0,    &GsDumpState::Context::tex1,
    &GsDumpState::Context::clamp,    &GsDumpState::Context::miptbp1, &GsDumpState::Context::miptbp2,
    &GsDumpState::Context::scissor,  &GsDumpState::Context::alpha,   &GsDumpState::Context::test,
    &GsDumpState::Context::fba,      &GsDumpState::Context::frame,   &GsDumpState::Context::zbuf,
};
// Their GIF A+D addresses (context 1's; context 2's is one more), in the same order.
constexpr uint8_t kContextAddress[] = {0x18, 0x06, 0x14, 0x08, 0x34, 0x36, 0x40, 0x42, 0x47, 0x4A, 0x4C, 0x4E};

}  // namespace

std::vector<uint8_t> GsDumpState::encode() const {
    std::vector<uint8_t> out(kBytes, 0);
    uint8_t* p = out.data();
    store<uint32_t>(p, kVersion);
    for (size_t i = 0; i < std::size(kEnvOrder); ++i) store<uint64_t>(p + kEnv + i * 8, this->*kEnvOrder[i]);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < std::size(kContextOrder); ++i)
            store<uint64_t>(p + kContexts + (c * 12 + i) * 8, context[c].*kContextOrder[i]);
    store<uint64_t>(p + kVertex, rgbaq);
    store<uint64_t>(p + kVertex + 8, st);
    store<uint32_t>(p + kVertex + 16, uv);
    store<uint32_t>(p + kVertex + 20, fog);
    store<uint64_t>(p + kVertex + 24, xyz);
    std::memcpy(p + kTransfer, transfer.data(), transfer.size());
    std::memcpy(p + kRegisterBytes, memory.data(), std::min(memory.size(), kMemoryBytes));
    std::memcpy(p + kPaths, paths.data(), paths.size());
    store<float>(p + kQ, q);
    return out;
}

bool GsDumpState::decode(const uint8_t* bytes, size_t size, std::string& error) {
    if (size != kBytes) {
        error = "a GS state of " + std::to_string(size) + " bytes, not " + std::to_string(kBytes) + " (version 9)";
        return false;
    }
    if (const uint32_t version = load<uint32_t>(bytes); version != kVersion) {
        error = "GS state version " + std::to_string(version) + ", not 9";
        return false;
    }
    for (size_t i = 0; i < std::size(kEnvOrder) - 1; ++i) this->*kEnvOrder[i] = load<uint64_t>(bytes + kEnv + i * 8);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < std::size(kContextOrder); ++i)
            context[c].*kContextOrder[i] = load<uint64_t>(bytes + kContexts + (c * 12 + i) * 8);
    rgbaq = load<uint64_t>(bytes + kVertex);
    st = load<uint64_t>(bytes + kVertex + 8);
    uv = load<uint32_t>(bytes + kVertex + 16);
    fog = load<uint32_t>(bytes + kVertex + 20);
    xyz = load<uint64_t>(bytes + kVertex + 24);
    std::memcpy(transfer.data(), bytes + kTransfer, transfer.size());
    memory.assign(bytes + kRegisterBytes, bytes + kRegisterBytes + kMemoryBytes);
    std::memcpy(paths.data(), bytes + kPaths, paths.size());
    q = load<float>(bytes + kQ);
    return true;
}

std::vector<uint64_t> GsDumpState::registersAsGif() const {
    std::vector<std::pair<uint32_t, uint64_t>> writes = {   // (the register's GIF address, its value)
        {0x00, prim},      {0x1A, prmodecont}, {0x1C, texclut}, {0x22, scanmsk},   {0x3B, texa},
        {0x3D, fogcol},    {0x44, dimx},       {0x45, dthe},    {0x46, colclamp},  {0x49, pabe},
        {0x50, bitbltbuf}, {0x51, trxpos},     {0x52, trxreg},
        // PRMODE as PRIM's attributes (bits 3-10): with PRMODECONT.AC = 0 they come from PRMODE, and PCSX2's state
        // keeps only the effective PRIM (lane A)
        {0x1B, prim & 0x7F8},
    };
    for (uint8_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < std::size(kContextOrder); ++i)
            writes.push_back({uint32_t(kContextAddress[i]) + c, context[c].*kContextOrder[i]});
    writes.push_back({0x01, rgbaq});
    writes.push_back({0x02, st});
    writes.push_back({0x03, uv});
    writes.push_back({0x0A, uint64_t(fog & 0xFF) << 56});
    std::vector<uint64_t> words;   // (lo, hi) per quadword
    // The GIFtag: NLOOP, EOP, PACKED, NREG 1; its one register descriptor A+D.
    words.push_back(uint64_t(writes.size()) | (uint64_t(1) << 15) | (uint64_t(1) << 60));
    words.push_back(0xE);
    for (const auto& [address, value] : writes) {
        words.push_back(value);
        words.push_back(address);
    }
    return words;
}

GsDumpWriter::~GsDumpWriter() {
    std::string ignored;
    close(ignored);
}

void GsDumpWriter::put(const void* data, size_t bytes) {
    if (!file || !bytes) return;
    if (std::fwrite(data, 1, bytes, file) != bytes) failed = true;
}

bool GsDumpWriter::open(const std::string& path, const GsDumpState& state, const uint8_t* privileged,
                        std::string& error, const std::string& serial, uint32_t crc) {
    std::string ignored;
    close(ignored);
    file = std::fopen(path.c_str(), "wb");
    if (!file) {
        error = "cannot write " + path;
        return false;
    }
    failed = false;
    // The header: a CRC of FFFFFFFF says a full header follows; its size, then nine words (no screenshot).
    const uint32_t marker = 0xFFFFFFFFu;
    const uint32_t headerWords[9] = {GsDumpState::kVersion, static_cast<uint32_t>(GsDumpState::kBytes), 36,
                                     static_cast<uint32_t>(serial.size()), crc, 0, 0,
                                     36 + static_cast<uint32_t>(serial.size()), 0};
    const uint32_t headerSize = static_cast<uint32_t>(sizeof headerWords + serial.size());
    put(&marker, 4);
    put(&headerSize, 4);
    put(headerWords, sizeof headerWords);
    put(serial.data(), serial.size());
    const std::vector<uint8_t> bytes = state.encode();
    put(bytes.data(), bytes.size());
    put(privileged, GsDumpState::kPrivilegedBytes);
    if (failed) error = "writing " + path + " failed";
    return !failed;
}

void GsDumpWriter::transfer(uint32_t path, const uint8_t* data, size_t bytes) {
    if (!bytes) return;
    const uint8_t head[2] = {0, static_cast<uint8_t>(path)};
    const uint32_t size = static_cast<uint32_t>(bytes);
    put(head, 2);
    put(&size, 4);
    put(data, bytes);
}

void GsDumpWriter::readFifo(uint32_t qwc) {
    if (!qwc) return;
    const uint8_t id = 2;
    put(&id, 1);
    put(&qwc, 4);
}

void GsDumpWriter::vsync(uint32_t field, const uint8_t* privileged) {
    const uint8_t regs = 3;
    put(&regs, 1);
    put(privileged, GsDumpState::kPrivilegedBytes);
    const uint8_t sync[2] = {1, static_cast<uint8_t>(field & 1)};
    put(sync, 2);
}

bool GsDumpWriter::close(std::string& error) {
    if (!file) return true;
    const bool closed = std::fclose(file) == 0;
    file = nullptr;
    if (failed || !closed) {
        error = "writing the GS dump failed";
        return false;
    }
    return true;
}

bool GsDump::read(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto need = [&](size_t at, size_t bytes) {
        if (at + bytes <= d.size()) return true;
        error = path + ": ends inside " +
                (at < 8 ? std::string("the header") : "a packet at byte " + std::to_string(at));
        return false;
    };
    if (!need(0, 8 + 36)) return false;
    if (load<uint32_t>(d.data()) != 0xFFFFFFFFu) {
        error = path + ": not a GS dump with PCSX2's full header (its first word is not FFFFFFFF)";
        return false;
    }
    const uint32_t headerSize = load<uint32_t>(d.data() + 4);
    uint32_t h[9];
    std::memcpy(h, d.data() + 8, sizeof h);
    if (uint64_t(h[2]) + h[3] > headerSize || uint64_t(h[7]) + h[8] > headerSize || !need(8, headerSize)) {
        if (error.empty()) error = path + ": a header whose parts lie outside it";
        return false;
    }
    serial.assign(reinterpret_cast<const char*>(d.data() + 8 + h[2]), h[3]);
    crc = h[4];
    size_t at = 8 + headerSize;
    if (!need(at, h[1] + GsDumpState::kPrivilegedBytes)) return false;
    if (!state.decode(d.data() + at, h[1], error)) {
        error = path + ": " + error;
        return false;
    }
    at += h[1];
    privileged.assign(d.begin() + at, d.begin() + at + GsDumpState::kPrivilegedBytes);
    at += GsDumpState::kPrivilegedBytes;
    packets.clear();
    while (at < d.size()) {
        Packet p;
        p.id = d[at];
        if (p.id == 0) {
            if (!need(at, 6)) return false;
            p.path = d[at + 1];
            const uint32_t size = load<uint32_t>(d.data() + at + 2);
            if (!need(at + 6, size)) return false;
            p.data.assign(d.begin() + at + 6, d.begin() + at + 6 + size);
            at += 6 + size;
        } else if (p.id == 1) {
            if (!need(at, 2)) return false;
            p.path = d[at + 1];
            at += 2;
        } else if (p.id == 2) {
            if (!need(at, 5)) return false;
            p.qwc = load<uint32_t>(d.data() + at + 1);
            at += 5;
        } else if (p.id == 3) {
            if (!need(at, 1 + GsDumpState::kPrivilegedBytes)) return false;
            p.data.assign(d.begin() + at + 1, d.begin() + at + 1 + GsDumpState::kPrivilegedBytes);
            at += 1 + GsDumpState::kPrivilegedBytes;
        } else {
            error = path + ": an unknown packet " + std::to_string(p.id) + " at byte " + std::to_string(at);
            return false;
        }
        packets.push_back(std::move(p));
    }
    return true;
}

}  // namespace gt4::gs
