// The IOP side of the game, natively: the SIF RPC client library is replaced at its API, and every
// server the game binds is answered here (docs/IOP_SERVICES.md has the protocols). Also the IOP boot
// calls (reboot, module loads: all succeed), libcdvd's init, the pad (libpad2/libvib/libdbc) and the
// memory card (libmc), and the SDK's DelayThread / GetTimerSystemTime / iWakeupThread.
#include "audio.h"
#include "audio_tags.h"
#include "gt4/link/protocol.h"
#include "pad_pedals.h"
#include "pad_steer.h"
#include "vehicle.h"
#include "guest.h"
#include "image.h"
#include "kernel.h"
#include "link.h"
#include "load_stats.h"
#include "log.h"
#include "menu/menu_host.h"
#include "runtime.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <vector>

using namespace gt4::rt;

extern "C" int gt4_game_option(int id);   // runtime/sdk.cpp: the host's gameplay options

namespace {

// ---------------------------------------------------------------------------------------------
// Disc image

HANDLE g_iso = INVALID_HANDLE_VALUE;
// Music and movies (disc_read) read through a handle and a lock of their own: their reads, every few VBlanks, no longer
// wait behind a load's, nor a load behind theirs (PLAN.md D10). The same file, opened twice.
HANDLE g_iso_streams = INVALID_HANDLE_VALUE;
const bool g_trace_disc = getenv("GT4_TRACE_DISC") != nullptr;  // log every disc/stream request
std::mutex g_iso_mutex, g_streams_mutex;
constexpr uint32_t kLayer1Start = 1317040;  // absolute LBA of layer 1 (the image is linear)

// `load`: a file or sector read the game waits for (GT4_LOAD_STATS counts those; not music and movie streams)
bool iso_read(uint64_t offset, void *dst, uint32_t n, bool load = true) {
    const double started = load ? load_stats_begin() : 0.0;   // (GT4_LOAD_STATS: the disc's share of a load, D10; the lock's wait too)
    const bool own = !load && g_iso_streams != INVALID_HANDLE_VALUE;   // music and movies: their own handle
    std::lock_guard<std::mutex> lock(own ? g_streams_mutex : g_iso_mutex);
    const HANDLE file = own ? g_iso_streams : g_iso;
    if (file == INVALID_HANDLE_VALUE) return false;
    // Until all of it is read: under Wine (Proton) a read can stop early, as the host's read() may (it did at 64 KB
    // on a network drive)
    char *out = static_cast<char *>(dst);
    uint32_t got = 0;
    while (got < n) {
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(offset + got);
        ov.OffsetHigh = static_cast<DWORD>((offset + got) >> 32);
        DWORD part = 0;
        if (!ReadFile(file, out + got, n - got, &part, &ov)) {
            static int reported = 0;
            if (reported++ < 8)
                log_warn("disc: reading %u bytes at %llx failed (Windows error %lu)", n - got,
                         static_cast<unsigned long long>(offset + got), GetLastError());
            break;
        }
        if (!part) break;   // the end of the image
        got += part;
    }
    if (got < n) std::memset(out + got, 0, n - got);
    if (load) load_stats_read(started, n);
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
        if (h < g_streams.size() && g_streams[h].used && spu < (2u << 20)) {
            const uint32_t len = std::min<uint32_t>(n, (2u << 20) - spu);
            // read beside SPU RAM, then copied in under the audio lock: the game's and a host mixer's voices both see
            // the write at once, between two frames (spu_ram_write, PLAN.md D5)
            std::vector<uint8_t> data(len);
            iso_read(g_streams[h].pos, data.data(), len);
            // (GT4_TRACE_D9: D9's car sound banks are the SPU RAM regions loaded here; the disc offset names the file)
            static const bool trace_d9 = getenv("GT4_TRACE_D9") != nullptr;
            if (trace_d9)
                log_info("d9: spu load %06x + %x from disc %llx (frame %u)", spu, len,
                         static_cast<unsigned long long>(g_streams[h].pos), kernel_frame_count());
            spu_ram_write(spu, data.data(), len);
            g_streams[h].pos += n;
        }
        break;
    }
    default:
        break;  // 6/7 host write streams, 8, 9 (release the disc hold token)
    }
}

// ---- PBGM: streamed music (runtime/audio.cpp plays the .ads); VOIC, MPG1/MPG2: silent for now

void pbgm(uint32_t fno, uint8_t *send, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    const int32_t arg = send ? static_cast<int32_t>(rd32(send)) : 0;
    // (init, play and stop; the volume and fade calls come every frame)
    if (fno <= 2) log_info("PBGM %u (%d)", fno, arg);
    switch (fno) {
    case 0:
        if (recv) wr32(recv, 0x107);
        break;
    case 1:  // play: +0 LBA, +4 size, +8 u8 loop
        bgm_play(rd32(send), rd32(send + 4), send[8] != 0);
        break;
    case 2:
        bgm_stop();
        break;
    case 3:
        bgm_fade(arg);
        break;
    case 4:
        bgm_volume(arg);
        break;
    case 6:
    case 7:
        bgm_pause(fno == 6);
        break;
    case 8:
        if (recv && rsize >= 0x40) bgm_status(recv);
        break;
    case 9:
        bgm_mono(arg != 0);
        break;
    default:
        break;
    }
}

void voic(uint32_t fno, uint8_t *, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);
    if (fno == 1 && recv) wr32(recv, 1);       // a PCM output handle
    if (fno == 3) Sleep(53);                     // the reply paces the 0x2800-byte blocks
}

void mpg(uint32_t, uint8_t *, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (recv && rsize) std::memset(recv, 0, rsize);  // MPG1 2: a zero slot header ends the movie
}

// ---- SPUP: the SPU2 register image every VBlank (runtime/audio.cpp mixes the voices); the
// reply is ENDX + ENVX per core

void spup(uint32_t fno, uint8_t *send, uint32_t, uint8_t *recv, uint32_t rsize) {
    if (fno == 4 && send) {  // send and receive may share the buffer: read first
        voice_tags_trace(send);   // (GT4_TRACE_D9, debugging: D9 step 1's measurements)
        spu_apply(send);
    }
    if (recv && rsize) std::memset(recv, 0, rsize);
    if (fno == 0 && recv) {
        wr32(recv, 0x112);
        return;
    }
    if ((fno == 4 || fno == 3) && recv && rsize >= 0x68) spu_status(recv);
}

// ---- Logitech wheel (LGDEV): version 0x010B2400; a Driving Force Pro while the host has a wheel (PLAN.md D7)

// GT4's LGDEV client (docs/IOP_SERVICES.md 5.8; the map in spikes/R15-lgdev-effects.md) sees one Driving Force Pro
// while the host sends a connected wheel (GT4_REC_WHEEL, input.wheel.mode = dfp), or always with GT4_FAKE_DFP=1
// (debugging: D7's bench, pad 1's state as the wheel's). With neither, every call but init and the state save and
// restore answers "no device", as before D7.
//   Discovery: device info (fno 1) index 0 found, index 1 not; open (2) handle 1; property 0x10005 (4) the
//   signature 0x046DC298. GT4 rescans every VBlank while it has no wheel open (func_005552B8), so a plug is found
//   there. At an unplug the read fails and the notify area's first word (the EE address fno 12 gives at +0x5C) is set
//   to 1: GT4 reads and clears it (func_00560A88), unloads the effects, closes the wheel and rescans.
//   The input read (6): the wheel's steering at +0x72, its pedals at +0x7E and +0x80, the hat at +0x82, the
//   buttons at +0x84. GT4 drives the effects only for the controller in use, so a wheel that isn't read gets none.
//   Effects: each open's downloads (7) get handles 0x100-0x102, in GT4's order (A, B, C). Their blocks (7, 13) and
//   states (8 start, 9 stop, 10 unload, 15 / 16 the sets) go to the host as GT4_REC_WHEEL_FX when one changes, at most
//   once a VBlank: a change in a VBlank already sent goes with the next call, and GT4 reads the wheel every VBlank
//   while it has it open. The host decides what the wheel plays (its stops are its own: PLAN.md D7).
// One GT4 thread makes every LGDEV call (the VBlank thread 005552B8; the asynchronous wrappers have no caller), so the
// state needs no lock. Logged ("lgdev:"): opens, closes, properties, unplugs; with GT4_FAKE_DFP=1 or
// GT4_TRACE_LGDEV=1, every effect call and a call count every 600 VBlanks too.
bool ds2_buttons(int port, int8_t out[16]);   // (below: GT4's DualShock 2 assignment)

bool env_on(const char *name) {
    const char *e = std::getenv(name);
    return e && *e && *e != '0';
}
const bool g_fake_dfp = env_on("GT4_FAKE_DFP");
const bool g_lgdev_trace = g_fake_dfp || env_on("GT4_TRACE_LGDEV");

struct Lgdev {
    uint32_t notify = 0;              // GT4's notify area (fno 12)
    bool open = false;                // GT4 has the wheel open (2) and hasn't closed it (3)
    bool unplugged = false;           // the read failed and GT4 was told; logged once
    uint32_t next_effect = 0x100;
    Gt4WheelFx fx{};                  // what GT4_REC_WHEEL_FX sends
    bool fx_pending = false;
    uint32_t fx_sent_frame = ~0u;
    uint64_t calls[19] = {};
    uint32_t summary = 0;
};
Lgdev g_lgdev;

// The wheel now: the host's, or pad 1's under GT4_FAKE_DFP. False: no wheel.
bool wheel_now(Gt4LinkWheel &w) {
    if (!g_fake_dfp) return link_wheel(w);
    w = Gt4LinkWheel{};
    w.connected = 1;
    uint8_t p[18];
    if (!link_pad(0, p)) {
        std::memset(p, 0, sizeof p);
        p[4] = 0x80;
    }
    // the steering from the left stick; the pedals, the pressures of the buttons GT4 has accelerate and brake on (its
    // assignment); no buttons and no hat: the pad keeps the menus
    int8_t buttons[16];
    uint8_t accelerate = 0, brake = 0;
    if (ds2_buttons(0, buttons))
        for (int key = 0; key < 16; ++key) {
            const int k = pad::kPressureOf[key];
            if (k < 0) continue;
            if (buttons[key] == pad::kAccelerate) accelerate = std::max(accelerate, p[6 + k]);
            if (buttons[key] == pad::kBrake) brake = std::max(brake, p[6 + k]);
        }
    w.steering = static_cast<int16_t>(std::clamp((static_cast<int>(p[4]) - 128) * 256, -32767, 32767));
    w.pedal[0] = static_cast<uint16_t>(accelerate << 8);
    w.pedal[1] = static_cast<uint16_t>(brake << 8);
    w.hat = 8;
    return true;
}

void lgdev_fx_send(uint32_t frame) {
    if (!g_lgdev.fx_pending || frame == g_lgdev.fx_sent_frame) return;
    g_lgdev.fx_pending = false;
    g_lgdev.fx_sent_frame = frame;
    g_lgdev.fx.index = 0;
    g_lgdev.fx.count = GT4_WHEEL_FX_EFFECTS;
    link_send(GT4_REC_WHEEL_FX, &g_lgdev.fx, sizeof g_lgdev.fx);
}

Gt4WheelEffect *lgdev_effect(uint32_t handle) {
    const uint32_t i = handle - 0x100;
    return i < GT4_WHEEL_FX_EFFECTS ? &g_lgdev.fx.effects[i] : nullptr;
}

// A downloaded effect's new state (start, stop; unload is NONE from any state).
void lgdev_effect_state(uint32_t handle, uint32_t state) {
    Gt4WheelEffect *e = lgdev_effect(handle);
    if (!e || e->state == state || (e->state == GT4_WHEEL_FX_NONE && state != GT4_WHEEL_FX_NONE)) return;
    e->state = state;
    g_lgdev.fx_pending = true;
}

void lgdev_forget_effects() {
    for (Gt4WheelEffect &e : g_lgdev.fx.effects)
        if (e.state != GT4_WHEEL_FX_NONE) {
            e.state = GT4_WHEEL_FX_NONE;
            g_lgdev.fx_pending = true;
        }
}

const char *hex(const uint8_t *p, int n) {
    static char s[3 * 64 + 1];
    for (int i = 0; i < n && i < 64; ++i) std::snprintf(s + 3 * i, 4, "%02x ", p[i]);
    s[3 * std::min(n, 64) - 1] = 0;
    return s;
}

// fno 1-16 with a wheel, or with GT4 still holding the one that left.
void lgdev(uint32_t fno, uint8_t *b, const Gt4LinkWheel &w, bool wheel, uint32_t frame) {
    Lgdev &g = g_lgdev;
    wr32(b, 0);
    switch (fno) {
    case 1:   // device info: index 0 is the wheel (slot 0: the info's byte +0xC, at +0x7C, 0)
        if (rd32(b + 4) == 0 && wheel) std::memset(b + 0x70, 0, 0x18);
        else wr32(b, 0x80000002);
        break;
    case 2:   // open: a new set of effects follows
        g.open = true;
        g.unplugged = false;
        g.next_effect = 0x100;
        lgdev_forget_effects();
        wr32(b + 8, 1);
        log_info("lgdev: %u open index %u -> handle 1%s", frame, rd32(b + 4), g_fake_dfp ? " (GT4_FAKE_DFP)" : "");
        break;
    case 3:
        g.open = false;
        lgdev_forget_effects();
        log_info("lgdev: %u close handle %u", frame, rd32(b + 8));
        break;
    case 4:   // get property: 0x10005 the signature, VID << 16 | PID
        wr32(b + 0x5C, rd32(b + 0x58) == 0x10005 ? 0x046DC298u : 0u);
        log_info("lgdev: %u get property %x", frame, rd32(b + 0x58));
        break;
    case 5:
        log_info("lgdev: %u set property %x = %d", frame, rd32(b + 0x58), static_cast<int32_t>(rd32(b + 0x5C)));
        break;
    case 6:   // the input read (spikes/R15-lgdev-effects.md "Input"): +0x70 type 0
        if (!wheel) {   // unplugged: GT4 is told to rescan (it unloads, closes, and finds none)
            wr32(b, 0x80000006);
            if (g.notify) wr32(gp(g.notify), 1);
            if (!g.unplugged) log_info("lgdev: %u the wheel left; GT4 told to rescan", frame);
            g.unplugged = true;
            break;
        }
        std::memset(b + 0x70, 0, 0x18);
        std::memcpy(b + 0x72, &w.steering, 2);
        std::memcpy(b + 0x7E, &w.pedal[0], 2);
        std::memcpy(b + 0x80, &w.pedal[1], 2);
        b[0x82] = w.hat;
        std::memcpy(b + 0x84, &w.buttons, 4);
        break;
    case 7:      // download an effect: its 60-byte block at +0x70, the new handle at +0x18
    case 13: {   // update an effect
        uint32_t h = rd32(b + 0x18);
        if (fno == 7) {
            h = g.next_effect++;
            wr32(b + 0x18, h);
        }
        Gt4WheelEffect *e = lgdev_effect(h);
        if (!e) {
            log_warn("lgdev: %u %s of effect %x: past GT4's three", frame, fno == 7 ? "download" : "update", h);
            break;
        }
        const bool changed = std::memcmp(e->block, b + 0x70, GT4_WHEEL_FX_BLOCK) != 0;
        if (fno == 7 || changed) {
            std::memcpy(e->block, b + 0x70, GT4_WHEEL_FX_BLOCK);
            if (fno == 7) e->state = GT4_WHEEL_FX_STOPPED;
            g.fx_pending = true;
            if (g_lgdev_trace)
                log_info("lgdev: %u %s effect %x: %s", frame, fno == 7 ? "download" : "update", h,
                         hex(b + 0x70, GT4_WHEEL_FX_BLOCK));
        }
        break;
    }
    case 8: case 9: case 10: {
        const uint32_t h = rd32(b + 0x18);
        lgdev_effect_state(h, fno == 8 ? GT4_WHEEL_FX_RUNNING : fno == 9 ? GT4_WHEEL_FX_STOPPED : GT4_WHEEL_FX_NONE);
        if (g_lgdev_trace)
            log_info("lgdev: %u %s effect %x", frame, fno == 8 ? "start" : fno == 9 ? "stop" : "unload", h);
        break;
    }
    case 15: case 16: {   // start / stop several: the count at +0x5C, the handles from +0x18
        const uint32_t n = std::min<uint32_t>(rd32(b + 0x5C), 16);
        static uint32_t lastSet[2][17] = {};
        uint32_t now[17] = {n};
        for (uint32_t i = 0; i < n; ++i) {
            now[1 + i] = rd32(b + 0x18 + 4 * i);
            lgdev_effect_state(now[1 + i], fno == 15 ? GT4_WHEEL_FX_RUNNING : GT4_WHEEL_FX_STOPPED);
        }
        if (g_lgdev_trace && std::memcmp(lastSet[fno - 15], now, sizeof now)) {   // (once a VBlank: when it changes)
            std::memcpy(lastSet[fno - 15], now, sizeof now);
            char list[16 * 9 + 1] = {};
            for (uint32_t i = 0; i < n; ++i) std::snprintf(list + 9 * i, 10, "%08x ", now[1 + i]);
            log_info("lgdev: %u %s %u: %s", frame, fno == 15 ? "start set" : "stop set", n, list);
        }
        break;
    }
    default:
        break;
    }
}

void logitech(uint32_t fno, uint8_t *send, uint32_t, uint8_t *, uint32_t) {
    // send and receive share one buffer, processed in place; result code at +0
    Lgdev &g = g_lgdev;
    const uint32_t frame = kernel_frame_count();
    if (fno == 12) {
        g.notify = rd32(send + 0x5C);
        if (g.notify) {
            std::memset(gp(g.notify), 0, 64);
            gp(g.notify)[0] = 1;
        }
        wr32(send, 0);
        wr32(send + 4, 0x010B2400);
        return;
    }
    if (fno == 17 || fno == 18) {
        wr32(send, 0);
        return;
    }
    if (fno < 19) ++g.calls[fno];
    Gt4LinkWheel w{};
    const bool wheel = wheel_now(w);
    if (wheel || g.open) lgdev(fno, send, w, wheel, frame);
    else wr32(send, 0x80000006);   // no device
    lgdev_fx_send(frame);
    if (g_lgdev_trace && frame - g.summary >= 600) {
        g.summary = frame;
        auto c = [&](int f) { return static_cast<unsigned long long>(g.calls[f]); };
        log_info("lgdev: %u calls: info %llu open %llu prop %llu/%llu read %llu download %llu update %llu start %llu "
                 "stop %llu unload %llu set %llu/%llu", frame, c(1), c(2), c(4), c(5), c(6), c(7), c(13), c(8), c(9),
                 c(10), c(15), c(16));
    }
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
    // The request is four words, option, port, slot, number (docs/IOP_SERVICES.md, 00551810: {2, 0 / 1, 0, 0}):
    // the socket id is the port, so each port reads its own pad. (Byte 1, the option word's, was 0 for both: port 2
    // read pad 1, and a menu that takes either pad saw every press twice.)
    return param ? static_cast<int>(rd32(param + 4) & 1) : 0;
}
extern "C" int gt4_replace_scePad2DeleteSocket(int) { return 1; }
// 1 (stable) for a port with a pad, 0 for an empty one: what libpad2 returns when nothing answers on the port (GT4's
// pad update, 00551580, then clears that port's record and does not read it). Port 1 always has the host's pad (the
// keyboard at least); port 2 only while the host sends one (a second gamepad).
extern "C" int gt4_replace_scePad2GetState(int socket) {
    if ((socket & 1) == 0) return 1;
    uint8_t data[18];
    return link_pad(1, data) ? 1 : 0;
}
// GT4_PAD_SCRIPT=VBLANK:BUTTON[:FRAMES],...: on top of the host's pad, hold BUTTON on pad 1 for FRAMES
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

// GT4's DualShock 2 assignment of a port where its race reads it (RaceSolitaire's constructor: *0x00622F4C +
// 0x39E80, the ports, 0xB8 bytes each from +4; the DualShock 2 config at +0x18, its vtable 0x00688210 at +0xC, the
// 16 buttons at +0x1D). False when that isn't there (early in the boot, or not a DualShock 2 config).
bool ds2_buttons(int port, int8_t out[16]) {
    const uint32_t game = rd32(gp(0x00622F4C));
    const uint32_t ds2 = game + 0x39E80 + 4 + static_cast<uint32_t>(port) * 0xB8 + 0x18;
    if ((game & 3) || game < kGuestSectionStart || !in_guest_ram(ds2, 0x2D)) return false;
    if (rd32(gp(ds2 + 0xC)) != 0x00688210) return false;
    std::memcpy(out, gp(ds2 + 0x1D), 16);
    return true;
}

// input.steer.game_band (the host's kOptionSteerBand, permille; runtime/pad_steer.h): GT4's steering band compensated
// in the left stick's byte, on each port, only while a race's physics runs (vehicle_live(): some car has weight on a
// wheel; the conductor alone is in the menus too): the band is the car's, and menus outside a race read the byte as they
// always did. A race's own pause menu and its replays see the remapped byte.
void steer_band_pad(uint8_t *data) {
    const int permille = gt4_game_option(static_cast<int>(kOptionSteerBand));
    if (permille <= 0 || !vehicle_live()) return;
    data[4] = pad::steer_band(data[4], static_cast<uint32_t>(permille));   // (bytes 2..5 the sticks; 4 left X)
}

// input.trigger_travel = full (the host's kOptionTriggerTravel, live; GT4_TRIGGER_TRAVEL=game|full overrides it for
// tests): the pedals' pressures rescaled to GT4's range (runtime/pad_pedals.h), on the buttons GT4's assignment has
// accelerate and brake on. Where the assignment can't be read (early in the boot, or a config that isn't a DUALSHOCK 2
// one) nothing is scaled: the game's own behaviour. GT4_TRACE_PAD=pedals logs the pressures when they change.
void scale_pedals(int port, uint8_t *data) {
    static const int forced = [] {
        const char *e = std::getenv("GT4_TRIGGER_TRAVEL");
        if (!e || !*e) return -1;
        if (!std::strcmp(e, "full")) return 1;
        if (std::strcmp(e, "game")) log_warn("GT4_TRIGGER_TRAVEL=%s: expected game or full", e);
        return 0;
    }();
    const bool full = forced >= 0 ? forced == 1 : gt4_game_option(static_cast<int>(kOptionTriggerTravel)) == 1;
    static const bool trace = std::getenv("GT4_TRACE_PAD") && !std::strcmp(std::getenv("GT4_TRACE_PAD"), "pedals");
    if (!full && !trace) return;
    int8_t buttons[16];
    const bool read = ds2_buttons(port, buttons);
    uint8_t before[18];
    std::memcpy(before, data, 18);
    if (full && read) pad::rescale_pedals(data, buttons);
    if (trace) {
        static uint8_t last[2][12] = {};
        if (std::memcmp(last[port], data + 6, 12)) {
            std::memcpy(last[port], data + 6, 12);
            if (!read) {
                log_info("pad: pedals, port %d: assignment unknown: not scaled", port);
                return;
            }
            char line[160];
            int n = 0;
            for (int key = 0; key < 12; ++key)
                if (buttons[key] == pad::kAccelerate || buttons[key] == pad::kBrake) {
                    const int k = pad::kPressureOf[key];
                    n += std::snprintf(line + n, sizeof line - n, " %s key %d: %u -> %u", buttons[key] == pad::kAccelerate
                                       ? "accelerate" : "brake", key, before[6 + k], data[6 + k]);
                }
            log_info("pad: pedals, port %d (game's assignment, travel %s):%s", port, full ? "full" : "game", line);
        }
    }
}
}  // namespace

extern "C" int gt4_replace_scePad2Read(int id, uint8_t *data) {
    if (!link_pad(id & 1, data)) std::memcpy(data, g_pad[id & 1], 18);
    scale_pedals(id & 1, data);
    steer_band_pad(data);
    // GT4_VEHICLE_LOG (D6, D12; debugging): the cars' state once a VBlank (pad 1's read), with the steering byte this
    // read gives the game, for the lap and steering plots. On the pad's thread, so a line can mix two physics ticks;
    // the scene records come from the frame's kick (lane A's path).
    if ((id & 1) == 0 && vehicle_log_on()) vehicle_log(data);
    static const std::vector<ScriptedPress> script = pad_script();
    // GT4_TRACE_PAD=all: every read of pad 1, with its VBlank (how often a screen polls the pad)
    static const bool every = std::getenv("GT4_TRACE_PAD") && !std::strcmp(std::getenv("GT4_TRACE_PAD"), "all");
    if (every && (id & 1) == 0)
        log_info("pad: read at VBlank %u, socket %d, from %p", kernel_frame_count(), id, __builtin_return_address(0));
    if ((id & 1) == 0 && !script.empty()) {
        const uint32_t now = kernel_frame_count();
        for (const ScriptedPress &s : script)
            if (now >= s.from && now < s.until) {
                data[s.bit >> 3] &= static_cast<uint8_t>(~(1u << (s.bit & 7)));
                if (int k = pressure_index(s.bit); k >= 0) data[6 + k] = 0xFF;
                static uint32_t logged = ~0u;
                if (std::getenv("GT4_TRACE_PAD") && logged != s.from) {
                    logged = s.from;
                    log_info("pad: scripted press at VBlank %u (read %u): %02x %02x %02x %02x %02x %02x", s.from, now,
                             data[0], data[1], data[2], data[3], data[4], data[5]);
                }
            }
    }
    // The settings pages (runtime/menu, MENU_DESIGN 3.3): while one is open it reads this pad and GT4 gets a neutral
    // one, on every reader (the pad thread, the menus' MenuControl, the race's input); after it closes, until the
    // closing press is released. Last, so the pages see the same mapped and scripted pad GT4 would have.
    menu::pad_focus(id & 1, data);
    return 18;
}
extern "C" int gt4_replace_scePad2GetButtonProfile(int, uint8_t *profile) {
    std::memset(profile, 0xFF, 4);  // every DualShock 2 button, with pressure
    return 4;
}
// libvib for a DUALSHOCK 2: two actuators, 0 the small motor (on/off: 1 bit) and 1 the large one (0-255: 8 bits),
// profile byte 0x03 (one byte). The game sends their values packed from bit 0 in the order of the profile's bits
// (sceVibSetActParam); each change goes to the host as GT4_REC_RUMBLE (its SDL rumble, D2). GT4_TRACE_VIB=1 logs the
// raw profile and data the game sends.
extern "C" int gt4_replace_sceVibGetProfile(int, uint8_t *profile) {
    std::memset(profile, 0, 16);
    profile[0] = 0x03;
    return 1;
}
extern "C" int gt4_replace_sceVibSetActParam(int socket, int profileSize, const uint8_t *profile, int dataSize,
                                             const uint8_t *data) {
    const int port = socket & 1;
    uint32_t small = 0, large = 0;
    if (profile && data && profileSize >= 1 && dataSize >= 1) {
        const uint32_t bits = data[0] | (dataSize > 1 ? uint32_t(data[1]) << 8 : 0u);
        int at = 0;
        if (profile[0] & 1) small = bits & 1, at = 1;
        if (profile[0] & 2) large = (bits >> at) & 0xFF;
    }
    static const bool trace = std::getenv("GT4_TRACE_VIB") != nullptr;
    static uint32_t last[2][2] = {};
    if (trace && (last[port][0] != small || last[port][1] != large))
        log_info("vib: port %d, profile %d bytes %02x, data %d bytes %02x %02x: small %u, large %u", port, profileSize,
                 profile && profileSize > 0 ? profile[0] : 0, dataSize, data && dataSize > 0 ? data[0] : 0,
                 data && dataSize > 1 ? data[1] : 0, small, large);
    if (last[port][0] != small || last[port][1] != large) {
        last[port][0] = small;
        last[port][1] = large;
        const uint32_t payload[3] = {static_cast<uint32_t>(port), small, large};
        if (link_active()) link_send(GT4_REC_RUMBLE, payload, sizeof payload);
    }
    return 1;
}

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

bool disc_read(uint64_t offset, void *dst, uint32_t n) { return iso_read(offset, dst, n, false); }   // (music, movies)

void iop_init() {
    pad_neutral();
    const char *iso = g_config.iso_path;
    // No FILE_FLAG_RANDOM_ACCESS (D10): GT4 reads a file from its start to its end, in blocks, so the OS's read-ahead
    // keeps the next blocks coming (the flag turned it off). Music and movies stream: sequential scan, on their own handle.
    g_iso = CreateFileA(iso, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_iso_streams = CreateFileA(iso, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (g_iso == INVALID_HANDLE_VALUE)
        log_warn("disc image %s cannot be opened (set GT4_ISO or --iso): disc reads return zeros", iso);
    else
        log_info("disc image: %s", iso);
}

}  // namespace gt4::rt
