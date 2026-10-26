// The IOP side of the game, natively: the SIF RPC client library is replaced at its API, and every
// server the game binds is answered here (docs/IOP_SERVICES.md has the protocols). Also the IOP boot
// calls (reboot, module loads: all succeed), libcdvd's init, the pad (libpad2/libvib/libdbc) and the
// memory card (libmc), and the SDK's DelayThread / GetTimerSystemTime / iWakeupThread.
#include "guest.h"
#include "image.h"
#include "kernel.h"
#include "link.h"
#include "log.h"
#include "runtime.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <vector>

using namespace gt4::rt;

namespace {

// ---------------------------------------------------------------------------------------------
// Disc image

HANDLE g_iso = INVALID_HANDLE_VALUE;
const bool g_trace_disc = getenv("GT4_TRACE_DISC") != nullptr;  // log every disc/stream request
std::mutex g_iso_mutex;
constexpr uint32_t kLayer1Start = 1317040;  // absolute LBA of layer 1 (the image is linear)

bool iso_read(uint64_t offset, void *dst, uint32_t n) {
    std::lock_guard<std::mutex> lock(g_iso_mutex);
    if (g_iso == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(offset);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD got = 0;
    if (!ReadFile(g_iso, dst, n, &got, &ov)) got = 0;
    if (got < n) std::memset(static_cast<char *>(dst) + got, 0, n - got);
    return got > 0;
}

// ---------------------------------------------------------------------------------------------
// SIF RPC client data (SDK 3.0.2 layout)

struct SifRpcData {
    uint32_t paddr, pid;
    int32_t tid;
    uint32_t mode;
};
struct SifClientData {
    SifRpcData rpcd;
    uint32_t command;
    uint32_t buff, cbuff;
    uint32_t end_function, end_param;
    uint32_t serve;
};

using Handler = void (*)(uint32_t fno, uint8_t *send, uint32_t ssize, uint8_t *recv, uint32_t rsize);

struct Server {
    uint32_t id;
    const char *name;
    Handler handler;
};

// EE addresses as the game passes them; the uncached mirrors are real mappings (runtime/guest.cpp)
uint8_t *gp(uint32_t addr) { return static_cast<uint8_t *>(guest_ptr(addr)); }
uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr32(uint8_t *p, uint32_t v) { std::memcpy(p, &v, 4); }

// ---- PCDV / Pcdv: disc sectors, clock

void pcdv(uint32_t fno, uint8_t *send, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    switch (fno) {
    case 0:
        if (recv) wr32(recv, 0x111);
        break;
    case 3: {
        const uint32_t lba = rd32(send), bytes = rd32(send + 4), dst = rd32(send + 8);
        const bool ok = iso_read(static_cast<uint64_t>(lba) * 2048, gp(dst), bytes);
        if (g_trace_disc) log_info("PCDV read lba %u bytes %u -> %08x%s", lba, bytes, dst, ok ? "" : " FAILED");
        if (recv) wr32(recv, ok ? 0 : 1);
        break;
    }
    case 4:
        if (recv) {
            wr32(recv, 1);
            wr32(recv + 4, kLayer1Start);
        }
        break;
    default:
        break;  // 1, 2: disc-identity check on/off
    }
}

uint32_t g_clock_addr = 0;
DWORD WINAPI clock_pusher(void *) {
    for (;;) {
        if (g_clock_addr) {
            // seconds since 2000-01-01 00:00 UTC (what the IOP derives from the RTC)
            const uint32_t secs = static_cast<uint32_t>(time(nullptr) - 946684800);
            uint8_t *p = gp(g_clock_addr);
            wr32(p, secs);
            wr32(p + 4, 0);
            wr32(p + 8, 0);   // drive state OK
            wr32(p + 12, 0);
        }
        Sleep(100);
    }
}

void pcdv_ctl(uint32_t fno, uint8_t *send, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    if (fno == 0) {
        static bool started = false;
        g_clock_addr = rd32(send);
        if (!started) {
            started = true;
            const uint32_t secs = static_cast<uint32_t>(time(nullptr) - 946684800);
            wr32(gp(g_clock_addr), secs);
            CreateThread(nullptr, 64 << 10, clock_pusher, nullptr, 0, nullptr);
        }
    } else if (fno == 1) {
        g_clock_addr = 0;
    }
}

// ---- STRP: file streams by LBA

struct Stream {
    uint64_t pos;  // byte offset in the image
    bool used;
};
std::vector<Stream> g_streams(64);
std::vector<uint8_t> g_spu_ram(2 << 20);

void strp(uint32_t fno, uint8_t *send, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    switch (fno) {
    case 0:
        if (recv) wr32(recv, 0x107);
        break;
    case 1:  // stat of a host path: not found
        if (recv) wr32(recv, 1);
        break;
    case 3: {  // open: +0 absolute LBA, +4 size, +0x14 path (empty = disc)
        const char *path = reinterpret_cast<const char *>(send + 0x14);
        uint32_t handle = 0;
        if (!*path) {
            for (uint32_t i = 1; i < g_streams.size(); ++i)
                if (!g_streams[i].used) {
                    g_streams[i] = {static_cast<uint64_t>(rd32(send)) * 2048, true};
                    handle = i;
                    break;
                }
        } else {
            log_warn("STRP: open of host file %s refused", path);
        }
        if (g_trace_disc) log_info("STRP open lba %u size %u -> handle %u", rd32(send), rd32(send + 4), handle);
        if (recv) wr32(recv, handle);
        break;
    }
    case 2: {
        const uint32_t h = rd32(send);
        if (h < g_streams.size()) g_streams[h].used = false;
        break;
    }
    case 4: {  // read: +0 handle, +4 EE address, +8 bytes
        const uint32_t h = rd32(send), dst = rd32(send + 4), n = rd32(send + 8);
        if (g_trace_disc) log_info("STRP read handle %u bytes %u -> %08x", h, n, dst);
        if (h < g_streams.size() && g_streams[h].used) {
            iso_read(g_streams[h].pos, gp(dst), n);
            g_streams[h].pos += n;
        }
        break;
    }
    case 5: {  // stream into SPU2 RAM: +0 handle, +4 SPU address, +8 bytes
        const uint32_t h = rd32(send), spu = rd32(send + 4), n = rd32(send + 8);
        if (h < g_streams.size() && g_streams[h].used && spu < g_spu_ram.size()) {
            const uint32_t len = std::min<uint32_t>(n, static_cast<uint32_t>(g_spu_ram.size()) - spu);
            iso_read(g_streams[h].pos, g_spu_ram.data() + spu, len);
            g_streams[h].pos += n;
        }
        break;
    }
    default:
        break;  // 6/7 host write streams, 8, 9 (release the disc hold token)
    }
}

// ---- PBGM, VOIC, MPG1/MPG2: streamed audio and movies (silent for now)

void pbgm(uint32_t fno, uint8_t *, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    if (fno == 0 && recv) wr32(recv, 0x107);
}

void voic(uint32_t fno, uint8_t *, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    if (fno == 1 && recv) wr32(recv, 1);       // a PCM output handle
    if (fno == 3) Sleep(53);                     // the reply paces the 0x2800-byte blocks
}

void mpg(uint32_t, uint8_t *, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);  // MPG1 2: a zero slot header ends the movie
}

// ---- SPUP: the SPU2 register image every VBlank; reply ENDX + ENVX per core

uint32_t g_keyed[2];  // voices keyed on and not keyed off

void spup(uint32_t fno, uint8_t *send, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    if (fno == 0 && recv) {
        wr32(recv, 0x112);
        return;
    }
    if (fno != 4 && fno != 3) return;
    if (fno == 4 && send) {
        for (int core = 0; core < 2; ++core) {
            const uint8_t *c = send + core * 0x1D0;
            const uint32_t flags = rd32(c);
            if (flags & 0x800) g_keyed[core] |= rd32(c + 0x1B4) & 0xFFFFFF;    // KON
            if (flags & 0x1000) g_keyed[core] &= ~(rd32(c + 0x1B8) & 0xFFFFFF);  // KOFF
        }
    }
    if (recv && rsize >= 0x68) {
        for (int core = 0; core < 2; ++core) {
            uint8_t *r = recv + core * 0x34;
            wr32(r, ~g_keyed[core] & 0xFFFFFF);  // ENDX: voices that have ended
            for (int v = 0; v < 24; ++v) {
                const uint16_t env = (g_keyed[core] >> v) & 1 ? 0x4000 : 0;
                std::memcpy(r + 4 + 2 * v, &env, 2);
            }
        }
    }
}

// ---- Logitech wheel: version 0x010B2400, no device

void logitech(uint32_t fno, uint8_t *send, uint32_t, uint8_t *, uint32_t) {
    // send and receive share one buffer, processed in place; result code at +0
    if (fno == 12) {
        const uint32_t notify = rd32(send + 0x5C);
        if (notify) {
            std::memset(gp(notify), 0, 64);
            gp(notify)[0] = 1;
        }
        wr32(send, 0);
        wr32(send + 4, 0x010B2400);
        return;
    }
    wr32(send, fno == 17 || fno == 18 ? 0 : 0x80000006);  // no device
}

// ---- everything else bound at boot: acknowledge with zeros

void ack(uint32_t, uint8_t *, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
}

const Server kServers[] = {
    {0x50434456, "PCDV", pcdv},       {0x50636476, "Pcdv", pcdv_ctl},  {0x53545250, "STRP", strp},
    {0x5042474D, "PBGM", pbgm},       {0x564F4943, "VOIC", voic},      {0x4D504731, "MPG1", mpg},
    {0x4D504732, "MPG2", mpg},        {0x53505550, "SPUP", spup},      {0x53505554, "SPUT", ack},
    {0x046D046D, "Logitech", logitech},
};

std::map<uint32_t, Server> g_bound;  // serve token -> server
std::mutex g_bind_mutex;

const Server *server_for(uint32_t id) {
    static Server generic{0, "generic", ack};
    for (const Server &s : kServers)
        if (s.id == id) return &s;
    generic.id = id;
    return &generic;
}

struct Completion {
    uint32_t fn, param;
};

void run_end_function(uint32_t p) {
    auto *c = reinterpret_cast<Completion *>(static_cast<uintptr_t>(p));
    reinterpret_cast<void (*)(uint32_t)>(guest_ptr(c->fn))(c->param);
    delete c;
}

// ---- pad state (neutral until the host sends input)

uint8_t g_pad[2][18];
void pad_neutral() {
    for (auto &p : g_pad) {
        std::memset(p, 0, sizeof p);
        p[0] = p[1] = 0xFF;                  // buttons, active low
        p[2] = p[3] = p[4] = p[5] = 0x80;    // sticks centred
    }
}



}  // namespace

extern "C" {
int gt4_sys_SetAlarm(uint32_t ticks, uint32_t func, uint32_t arg);
int gt4_sys_SleepThread();
int gt4_sys_GetThreadId();
int gt4_sys_WakeupThread(int);
}

namespace {
void delay_wake(int, uint16_t, uint32_t thread) { gt4_sys_WakeupThread(static_cast<int>(thread)); }
}  // namespace

// ---------------------------------------------------------------------------------------------
// SIF RPC

extern "C" int gt4_replace_sceSifInitRpc(int) { return 0; }
extern "C" int gt4_replace_sceSifMInitRpc(int) { return 0; }
extern "C" int gt4_replace_sceSifInitCmd() { return 0; }
extern "C" int gt4_replace_sceSifExitCmd() { return 0; }

extern "C" int gt4_replace_sceSifBindRpc(SifClientData *cd, uint32_t id, uint32_t) {
    KernelScope ks;
    const Server *s = server_for(id);
    cd->command = 0;
    cd->rpcd.pid = 0;
    // the serve pointer only has to be non-zero; it identifies the server for the calls
    cd->serve = reinterpret_cast<uint32_t>(s);
    {
        std::lock_guard<std::mutex> lock(g_bind_mutex);
        g_bound[cd->serve] = *s;
    }
    log_info("SIF: bound %s (%08x)%s", s->name, id, s->handler == ack && s->id != 0x53505554 ? " (acknowledged only)" : "");
    return 0;
}

extern "C" int gt4_replace_sceSifCallRpc(SifClientData *cd, uint32_t fno, uint32_t mode, uint32_t send,
                                         uint32_t ssize, uint32_t recv, uint32_t rsize, uint32_t end_fn,
                                         uint32_t end_param) {
    KernelScope ks;
    Server s{};
    {
        std::lock_guard<std::mutex> lock(g_bind_mutex);
        auto it = g_bound.find(cd->serve);
        if (it == g_bound.end()) {
            log_warn("SIF: call %u on an unbound client %08x", fno, guest_addr(cd));
            return -1;
        }
        s = it->second;
    }
    s.handler(fno, send ? gp(send) : nullptr, ssize, recv ? gp(recv) : nullptr, rsize);
    // Asynchronous calls finish in interrupt context, as the SIF DMA interrupt would; synchronous
    // callers do their own completion on return (the PDI clients signal their semaphore themselves).
    if ((mode & 1) && end_fn)
        kernel_post_callback(run_end_function, reinterpret_cast<uint32_t>(new Completion{end_fn, end_param}));
    return 0;
}

extern "C" int gt4_replace_sceSifCheckStatRpc(SifRpcData *) { return 0; }  // calls complete at once

extern "C" int gt4_replace_sceSifSetRpcQueue(uint32_t, int) { return 0; }
extern "C" int gt4_replace_sceSifRegisterRpc(uint32_t, uint32_t id, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    log_info("SIF: the EE registers server %08x (no IOP client will call it)", id);
    return 0;
}
extern "C" int gt4_replace_sceSifRpcLoop(uint32_t) {
    for (;;) gt4_sys_SleepThread();  // no request ever arrives
}
extern "C" int gt4_replace_sceSifGetOtherData(uint32_t, uint32_t src, uint32_t dst, int size, int) {
    std::memcpy(gp(dst), gp(src), static_cast<size_t>(size));
    return 0;
}

// ---- IOP boot and modules

extern "C" int gt4_replace_sceSifRebootIop(const char *img) {
    log_info("IOP: reboot with %s", img ? img : "(null)");
    return 1;
}
extern "C" int gt4_replace_sceSifSyncIop() { return 1; }
extern "C" int gt4_replace_sceSifLoadFileReset() { return 0; }
extern "C" int gt4_replace_sceFsReset() { return 0; }
extern "C" int gt4_replace_sceFsInit() { return 0; }
extern "C" int gt4_replace_sceSifInitIopHeap() { return 0; }
extern "C" uint32_t gt4_replace_sceSifAllocSysMemory(int, int, uint32_t) { return 0x00100000; }

static int g_next_module = 1;
extern "C" int gt4_replace_sceSifLoadModule(const char *path, int, const char *) {
    log_info("IOP: load module %s", path);
    return g_next_module++;
}
extern "C" int gt4_replace__sceSifLoadModule(const char *path, int, const char *, int *result, int) {
    log_info("IOP: load module %s", path);
    if (result) *result = 0;
    return g_next_module++;
}
extern "C" int gt4_replace_sceSifSearchModuleByName(const char *name) {
    log_info("IOP: search module %s: not loaded", name);
    return -1;
}

// ---- fileio (filestub.o): the game uses it only for host:/debug files and a network-config probe
// (docs/IOP_SERVICES.md 5.10); every device reports that the file or device does not exist.

namespace {
constexpr int kENOENT = -2, kENODEV = -19;
int fileio_absent(const char *what, const char *path, int error) {
    static int logged = 0;
    if (logged++ < 16) log_info("fileio: %s(%s): not available", what, path ? path : "");
    return error;
}
}  // namespace

extern "C" int gt4_replace_sceOpen(const char *path, int) { return fileio_absent("sceOpen", path, kENOENT); }
extern "C" int gt4_replace_sceClose(int) { return kENODEV; }
extern "C" int gt4_replace_sceLseek(int, int, int) { return kENODEV; }
extern "C" int64_t gt4_replace_sceLseek64(int, int64_t, int) { return kENODEV; }
extern "C" int gt4_replace_sceRead(int, void *, int) { return kENODEV; }
extern "C" int gt4_replace_sceWrite(int, const void *, int) { return kENODEV; }
extern "C" int gt4_replace_sceIoctl(int, int, void *) { return kENODEV; }
extern "C" int gt4_replace_sceIoctl2(int, int, void *, unsigned, void *, unsigned) { return kENODEV; }
extern "C" int gt4_replace_sceMkdir(const char *path, int) { return fileio_absent("sceMkdir", path, kENODEV); }
extern "C" int gt4_replace_sceFormat(const char *path, const char *, void *, int) {
    return fileio_absent("sceFormat", path, kENODEV);
}
extern "C" int gt4_replace_sceAddDrv(void *) { return kENODEV; }
extern "C" int gt4_replace_sceDopen(const char *path) { return fileio_absent("sceDopen", path, kENOENT); }
extern "C" int gt4_replace_sceDclose(int) { return kENODEV; }
extern "C" int gt4_replace_sceDread(int, void *) { return kENODEV; }
extern "C" int gt4_replace_sceGetstat(const char *path, void *) { return fileio_absent("sceGetstat", path, kENOENT); }
extern "C" int gt4_replace_sceChstat(const char *path, void *, unsigned) { return fileio_absent("sceChstat", path, kENOENT); }
extern "C" int gt4_replace_sceRename(const char *path, const char *) { return fileio_absent("sceRename", path, kENOENT); }
extern "C" int gt4_replace_sceSync(const char *, int) { return kENODEV; }
extern "C" int gt4_replace_sceMount(const char *, const char *path, int, void *, int) {
    return fileio_absent("sceMount", path, kENODEV);
}
extern "C" int gt4_replace_sceDevctl(const char *path, int, void *, unsigned, void *, unsigned) {
    return fileio_absent("sceDevctl", path, kENODEV);
}
extern "C" int gt4_replace_sceSymlink(const char *path, const char *) { return fileio_absent("sceSymlink", path, kENODEV); }
extern "C" int gt4_replace_sceReadlink(const char *path, char *, unsigned) {
    return fileio_absent("sceReadlink", path, kENODEV);
}
extern "C" int gt4_replace__sceCallCode(const char *path, int) { return fileio_absent("_sceCallCode", path, kENODEV); }
extern "C" int gt4_replace_sceFsSetIopBuf(int, int) { return 0; }
extern "C" int gt4_replace_sceFsSetIopPrio(int) { return 0; }

// ---- libcdvd (only init at boot)

extern "C" int gt4_replace_sceCdInit(int mode) {
    log_info("cdvd: sceCdInit(%d)", mode);
    return 1;
}
extern "C" int gt4_replace_sceCdMmode(int) { return 1; }

// ---- pad: libdbc, libpad2, libvib

extern "C" int gt4_replace_sceDbcInit() { return 1; }
extern "C" int gt4_replace_sceDbcEnd() { return 1; }
extern "C" int gt4_replace_scePad2Init(int) {
    pad_neutral();
    return 1;
}
extern "C" int gt4_replace_scePad2End() { return 1; }
extern "C" int gt4_replace_scePad2CreateSocket(const uint8_t *param, uint32_t) {
    // scePad2SocketParam: option, port, slot, number (one byte each in libpad2)
    return param ? param[1] & 1 : 0;
}
extern "C" int gt4_replace_scePad2DeleteSocket(int) { return 1; }
extern "C" int gt4_replace_scePad2GetState(int) { return 1; }  // stable
// GT4_PAD_SCRIPT=VBLANK:BUTTON[:FRAMES],...: without a host pad, hold BUTTON on pad 1 for FRAMES
// VBlanks (default 6) from VBLANK on, for unattended runs and captures. Buttons as gt4_host's
// ps2_pad.h: select l3 r3 start up right down left l2 r2 l1 r1 triangle circle cross square.
namespace {
struct ScriptedPress {
    uint32_t from, until;
    int bit;
};
std::vector<ScriptedPress> pad_script() {
    static const char *const names[16] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                          "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
    std::vector<ScriptedPress> out;
    const char *e = getenv("GT4_PAD_SCRIPT");
    for (const char *p = e; p && *p;) {
        char name[16] = "";
        unsigned at = 0, frames = 6;
        int n = 0;
        if (sscanf(p, "%u:%15[a-z0-9]%n", &at, name, &n) < 2) break;
        p += n;
        if (*p == ':') p += (sscanf(p, ":%u%n", &frames, &n) == 1) ? n : 1;
        for (int b = 0; b < 16; ++b)
            if (!std::strcmp(name, names[b])) out.push_back({at, at + frames, b});
        if (*p == ',') ++p;
    }
    return out;
}
// the pressure byte of each button (out[6 + k]), as ps2_pad.h orders them; -1: no pressure
int pressure_index(int bit) {
    static const int order[12] = {5, 7, 4, 6, 12, 13, 14, 15, 10, 11, 8, 9};
    for (int k = 0; k < 12; ++k)
        if (order[k] == bit) return k;
    return -1;
}
}  // namespace

extern "C" int gt4_replace_scePad2Read(int id, uint8_t *data) {
    if (link_pad(id & 1, data)) return 18;
    std::memcpy(data, g_pad[id & 1], 18);
    static const std::vector<ScriptedPress> script = pad_script();
    if ((id & 1) == 0 && !script.empty()) {
        const uint32_t now = kernel_vblank_count();
        for (const ScriptedPress &s : script)
            if (now >= s.from && now < s.until) {
                data[s.bit >> 3] &= static_cast<uint8_t>(~(1u << (s.bit & 7)));
                if (int k = pressure_index(s.bit); k >= 0) data[6 + k] = 0xFF;
            }
    }
    return 18;
}
extern "C" int gt4_replace_scePad2GetButtonProfile(int, uint8_t *profile) {
    std::memset(profile, 0xFF, 4);  // every DualShock 2 button, with pressure
    return 4;
}
extern "C" int gt4_replace_sceVibGetProfile(int, uint8_t *profile) {
    std::memset(profile, 0, 16);
    return 0;
}
extern "C" int gt4_replace_sceVibSetActParam(int, int, const uint8_t *, int, const uint8_t *) { return 1; }

// ---- memory card: runtime/memcard.cpp

// ---- SDK thread/timer helpers built on alarms and the timer the host does not emulate

extern "C" int gt4_replace_DelayThread(uint32_t usec) {
    {
        // the retry loops of the boot all wait here: name the first few callers
        static int logged = 0;
        if (logged < 3) {
            ++logged;
            log_backtrace("DelayThread called by");
        }
    }
    const uint32_t ticks = std::max<uint32_t>(1, static_cast<uint32_t>(usec * 15734.26 / 1e6));
    gt4_sys_SetAlarm(ticks, guest_addr(reinterpret_cast<const void *>(&delay_wake)),
                     static_cast<uint32_t>(gt4_sys_GetThreadId()));
    gt4_sys_SleepThread();
    return 0;
}
extern "C" uint64_t gt4_replace_GetTimerSystemTime() { return kernel_bus_cycles(); }
extern "C" int gt4_replace_iWakeupThread(int id) { return gt4_sys_WakeupThread(id); }

namespace gt4::rt {

bool disc_read(uint64_t offset, void *dst, uint32_t n) { return iso_read(offset, dst, n); }

void iop_init() {
    pad_neutral();
    const char *iso = g_config.iso_path;
    g_iso = CreateFileA(iso, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (g_iso == INVALID_HANDLE_VALUE)
        log_warn("disc image %s cannot be opened (set GT4_ISO or --iso): disc reads return zeros", iso);
    else
        log_info("disc image: %s", iso);
}

}  // namespace gt4::rt
