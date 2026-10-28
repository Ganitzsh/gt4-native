// The graphics boundary on the game side (docs/HW_ACCESS.md, docs/IOP_SERVICES.md section 4).
//
// The game's graphics library starts every VIF0/VIF1/GIF transfer through two functions:
// 004A1140(channel, address, qwc) (normal mode) and 004ABA50(channel, tag address) (source chain).
// Their replacements walk the DMA chain in guest memory and send what the channel would have
// delivered to gt4_host (VIF1 stream, VIF0 stream, GIF PATH3), then clear the channel's STR bit and
// raise its DMA-end interrupt so the game's handler (004AB6D8) advances. VIF1 and the GIF go in
// step, stopping where the hardware would for the game's interrupt handshakes (see "VIF1 and GIF
// (PATH3) in step" below); VIF0 and normal-mode transfers go at once.
// At every VBlank the GS privileged registers the game wrote are sent with a frame marker, and
// CSR.FIELD toggles as on an interlaced NTSC display.
#include "audio.h"
#include "guest.h"
#include "gt4/link/protocol.h"
#include "kernel.h"
#include "link.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace gt4::rt;

namespace {

constexpr uint32_t kGsPriv = 0x12000000;
constexpr uint32_t kGsCsr = 0x12001000;
constexpr uint32_t kDmaChanTable = 0x00629DC8;  // D_00629DC8: DMA channel register blocks, by channel
constexpr uint32_t kSprState = 0x70002000;      // spr_base(): the library state is at +0x50

struct ChannelOut {
    std::vector<uint8_t> data;
};

// A DMA address: the scratchpad, main RAM (physical, or its uncached mirrors 0x2/0x3xxxxxxx), or else
// natively a game thread's stack, which Windows places anywhere (004A12B8 builds the microprogram
// upload chain in a local array: masked to 32 MB it was read from the wrong memory, and VU1 never
// received a microprogram).
uint8_t *mem(uint32_t addr, bool spr) {
    if (spr || (addr & 0x70000000) == 0x70000000) return static_cast<uint8_t *>(guest_ptr(kScratchpad + (addr & 0x3FF0)));
    const uint32_t high = addr >> 28;
    if ((high == 0 || high == 2 || high == 3) && (addr & 0x0FFFFFFF) < 0x02000000)
        return static_cast<uint8_t *>(guest_ptr(addr & 0x01FFFFF0));
    return static_cast<uint8_t *>(guest_ptr(addr));  // as is: native stacks are only 4-aligned
}

void append(std::vector<uint8_t> &out, const uint8_t *p, uint32_t qwc) { out.insert(out.end(), p, p + qwc * 16); }

// Source-chain DMA (EE DMAC tag IDs): returns false on a malformed chain.
bool g_chain_has_mpg = false;  // the last chain walked uploaded a VU1 microprogram

// A source chain in progress: where the next tag is, the call stack (ASR0/1).
struct Chain {
    uint32_t tadr = 0;
    bool spr_tag = false;
    uint32_t asr[2] = {};
    int sp = 0;
    bool tte = false;
    uint32_t last_tag = 0;  // bits 0-31 of the last tag read (CHCR.TAG is its upper half)
};

// Walks a chain from c.tadr, appending what the channel delivers. With stop_irq, stops after the
// data of a tag with its IRQ bit (CHCR.TIE): returns 1 and c.tadr is the next tag. With one, stops
// after one tag: returns 2. Returns 0 at the chain's end, -1 if it does not end.
int walk(Chain &c, std::vector<uint8_t> &out, bool stop_irq, bool one = false) {
    uint32_t tadr = c.tadr & 0x7FFFFFFF;
    for (int guard = 0; guard < 1 << 20; ++guard) {
        const uint8_t *tag = mem(tadr, c.spr_tag);
        const bool tte = c.tte;
        uint32_t *asr = c.asr;
        int &sp = c.sp;
        bool &spr_tag = c.spr_tag;
        uint64_t lo;
        std::memcpy(&lo, tag, 8);
        c.last_tag = static_cast<uint32_t>(lo);
        const uint32_t qwc = static_cast<uint32_t>(lo & 0xFFFF);
        const uint32_t id = static_cast<uint32_t>(lo >> 28) & 7;
        const bool irq = (lo >> 31) & 1;
        const uint32_t addr = static_cast<uint32_t>(lo >> 32) & 0x7FFFFFFF;  // mem() aligns the PS2 forms
        const bool spr = (lo >> 63) & 1;
        if (tte) out.insert(out.end(), tag + 8, tag + 16);  // the tag's upper half (VIF codes) goes first
        if (tte && tag[15] == 0x4A) {  // an MPG (VU1 microprogram upload) in the tag
            g_chain_has_mpg = true;
            static int mpg_logged = 0;
            if (mpg_logged < 40) {
                ++mpg_logged;
                log_info("gs: VU1 microprogram upload, %u doublewords at %u (VBlank %u)", tag[14] ? tag[14] : 256u,
                         tag[12] | (tag[13] << 8), kernel_vblank_count());
            }
        }
        // GT4_DMA_LOG=N [GT4_DMA_LOG_FROM=VBLANK]: log the first N tags walked (from that VBlank on),
        // to find a packet's guest address (debugging)
        static long dma_log = getenv("GT4_DMA_LOG") ? atol(getenv("GT4_DMA_LOG")) : 0;
        static uint32_t dma_from = getenv("GT4_DMA_LOG_FROM") ? atol(getenv("GT4_DMA_LOG_FROM")) : 0;
        if (dma_log > 0 && kernel_vblank_count() >= dma_from) {
            --dma_log;
            log_info("dma tag %08x: id %u qwc %u addr %08x tte %d", tadr, id, qwc, addr, tte);
        }
        uint32_t next = 0;
        bool end = false;
        switch (id) {
        case 0:  // refe: QWC from ADDR, then end
            append(out, mem(addr, spr), qwc);
            end = true;
            break;
        case 1:  // cnt: QWC after the tag, next tag after the data
            append(out, tag + 16, qwc);
            next = tadr + 16 + qwc * 16;
            break;
        case 2:  // next: QWC after the tag, next tag at ADDR
            append(out, tag + 16, qwc);
            next = addr;
            spr_tag = spr;
            break;
        case 3:  // ref
        case 4:  // refs: QWC from ADDR, next tag after this one
            append(out, mem(addr, spr), qwc);
            next = tadr + 16;
            break;
        case 5:  // call: QWC after the tag, push the address after the data, next tag at ADDR
            append(out, tag + 16, qwc);
            if (sp >= 2) return -1;
            asr[sp++] = tadr + 16 + qwc * 16;
            next = addr;
            break;
        case 6:  // ret: QWC after the tag, pop
            append(out, tag + 16, qwc);
            if (sp == 0) end = true;
            else next = asr[--sp];
            break;
        case 7:  // end
            append(out, tag + 16, qwc);
            end = true;
            break;
        }
        if (end) {
            c.tadr = tadr;
            return 0;
        }
        tadr = next;
        if (irq && stop_irq) {  // the channel stops after this tag's data and interrupts
            c.tadr = tadr | (spr_tag ? 0x80000000u : 0);
            return 1;
        }
        if (one) {
            c.tadr = tadr | (spr_tag ? 0x80000000u : 0);
            return 2;
        }
    }
    log_warn("gs: DMA chain did not end (1M tags)");
    return -1;
}

// The whole chain at once (VIF0/VIF1, and GIF chains without tag interrupts).
bool walk_chain(uint32_t tadr, bool tte, std::vector<uint8_t> &out) {
    g_chain_has_mpg = false;
    Chain c;
    c.tadr = tadr & 0x7FFFFFFF;
    c.spr_tag = (tadr & 0x80000000) != 0;
    c.tte = tte;
    return walk(c, out, false) == 0;
}



// GS events written by PATH3 packets: FINISH (0x61), SIGNAL (0x60), LABEL (0x62) through A+D.
void scan_gif(const std::vector<uint8_t> &data, bool &finish, bool &signal) {
    size_t q = 0;
    const size_t nq = data.size() / 16;
    while (q < nq) {
        uint64_t lo, hi;
        std::memcpy(&lo, &data[q * 16], 8);
        std::memcpy(&hi, &data[q * 16 + 8], 8);
        ++q;
        const uint32_t nloop = lo & 0x7FFF;
        const uint32_t flg = (lo >> 58) & 3;
        uint32_t nreg = (lo >> 60) & 0xF;
        if (nreg == 0) nreg = 16;
        if (flg == 0) {  // PACKED
            for (uint32_t l = 0; l < nloop && q < nq; ++l)
                for (uint32_t r = 0; r < nreg && q < nq; ++r, ++q) {
                    if (((hi >> (4 * r)) & 0xF) != 0xE) continue;  // A+D
                    const uint32_t reg = data[q * 16 + 8];
                    if (reg == 0x61) finish = true;
                    if (reg == 0x60) signal = true;
                }
        } else if (flg == 1) {  // REGLIST: 64-bit entries, padded to a qword
            q += (nloop * nreg + 1) / 2;
        } else {  // IMAGE (and the disabled format): NLOOP qwords
            q += nloop;
        }
    }
}

void raise_gs_events(bool finish, bool signal) {
    if (!finish && !signal) return;
    auto &csr = guest_ref<uint32_t>(kGsCsr);
    if (finish) csr |= 2;
    if (signal) csr |= 1;
    kernel_raise_intc(0);
}

using WaitFn = int (*)(int);

// What a channel delivered, to the host.
void send_out(int channel, const std::vector<uint8_t> &out) {
    if (!out.empty()) {
        if (channel == 1) {
            link_send(GT4_REC_VIF1, out.data(), static_cast<uint32_t>(out.size()));
        } else if (channel == 0) {
            link_send(GT4_REC_VIF0, out.data(), static_cast<uint32_t>(out.size()));
        } else if (channel == 2) {
            const uint32_t path = 3;
            link_send(GT4_REC_GIF, &path, 4, out.data(), static_cast<uint32_t>(out.size()));
            if (!link_connected()) {  // a live host reports the events of every path (GS_EVENT)
                bool finish = false, signal = false;
                scan_gif(out, finish, signal);
                raise_gs_events(finish, signal);
            }
        }
    }
}

// The channel stops (STR clear) and interrupts at the end of the transfer.
void end_transfer(int channel) {
    const uint32_t regs = guest_ref<uint32_t>(kDmaChanTable + 4 * channel);
    guest_ref<uint32_t>(regs) &= ~0x100u;
    kernel_raise_dmac(channel);
}

void finish_transfer(int channel, std::vector<uint8_t> &out) {
    send_out(channel, out);
    end_transfer(channel);
}

void start_flag(int channel) {
    // the library's "transfer in flight" flag (scratchpad + 0x60 + 12n + 0xD), as the original sets it
    *static_cast<volatile uint8_t *>(guest_ptr(kSprState + 0x60 + 12 * channel + 0xD)) = 1;
}

uint32_t g_field = 0;
uint64_t g_priv_sent[16];
bool g_priv_valid[16];

void on_vblank(uint32_t count) {
    // CSR.FIELD (bit 13) alternates every VBlank on an interlaced display
    g_field ^= 1;
    auto &csr = guest_ref<uint32_t>(kGsCsr);
    csr = (csr & ~(1u << 13)) | (g_field << 13);
    audio_vblank();
    if (link_capture_finished() && !link_connected()) {  // a standalone capture run ends with its window
        link_capture_close();
        log_info("capture window closed at VBlank %u: exiting", count);
        ExitProcess(0);
    }
    if (!link_active()) return;
    static bool capturing = false;
    if (link_capture_window() != capturing) {  // a capture window opens: resend every register
        capturing = !capturing;
        std::memset(g_priv_valid, 0, sizeof g_priv_valid);
    }
    for (uint32_t i = 0; i < 15; ++i) {  // PMODE .. BGCOLOR
        const uint64_t v = guest_ref<uint64_t>(kGsPriv + i * 0x10);
        if (g_priv_valid[i] && v == g_priv_sent[i]) continue;
        g_priv_valid[i] = true;
        g_priv_sent[i] = v;
        struct {
            uint32_t reg, pad;
            uint64_t value;
        } rec{i * 0x10, 0, v};
        link_send(GT4_REC_GS_PRIV, &rec, sizeof rec);
    }
    const uint32_t rec[2] = {g_field, count};
    link_send(GT4_REC_VSYNC, rec, sizeof rec);
    link_poll();
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// VIF1 and GIF (PATH3) in step. GT4 streams textures through PATH3 while VIF1 draws, several sets
// into the same GS memory per frame, and keeps the two in step with interrupts: VIF1 FLUSH codes
// with the interrupt bit (NUM 1: wait for the next texture set, NUM 2: the current set is used,
// NUM 4) stop VIF1 until its handler (004AB548) resumes it, and GIF tags with IRQ (user bits 16:
// wait for VIF1, 17: a set is uploaded, 18) stop the GIF until its handler (004AB6D8) restarts it.
// Sending each stream whole drew every object with the frame's last texture set. Here both advance
// in turn, each transfer stopping where the hardware's would, and the game's handlers run at those
// points and say which stream goes on (D2_CHCR.STR, VIF1_FBRST.STC).

constexpr uint32_t kVif1Stat = 0x10003C00, kVif1Fbrst = 0x10003C10, kVif1Code = 0x10003C80;

// Length in 32-bit words of the VIF code at `w` with its data (STCYCL tracked for UNPACK).
struct VifScan {
    uint32_t cl = 1, wl = 1;
    size_t length(const uint32_t *w, size_t left) {
        const uint32_t code = w[0], cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
        size_t n = 1;
        if (cmd == 0x01) {
            cl = imm & 0xFF;
            wl = (imm >> 8) & 0xFF;
        } else if (cmd == 0x20) {
            n = 2;
        } else if (cmd == 0x30 || cmd == 0x31) {
            n = 5;
        } else if (cmd == 0x4A) {
            n = 1 + 2 * (num ? num : 256);
        } else if (cmd == 0x50 || cmd == 0x51) {
            n = 1 + 4 * size_t(imm ? imm : 65536);
        } else if (cmd >= 0x60) {
            const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3, count0 = num ? num : 256;
            const uint32_t count = wl <= cl ? count0 : cl * (count0 / wl) + std::min(count0 % wl, cl);
            n = 1 + (size_t(count) * (32 >> vl) * (vn + 1) + 31) / 32;
        }
        return std::min(n, left);
    }
};

// The GS events of a VIF1 stream's DIRECT packets (standalone runs: no host to report them).
void scan_vif_direct(const std::vector<uint8_t> &part, bool &finish, bool &signal) {
    const uint32_t *w = reinterpret_cast<const uint32_t *>(part.data());
    const size_t nw = part.size() / 4;
    VifScan scan;
    for (size_t pos = 0; pos < nw;) {
        const uint32_t cmd = (w[pos] >> 24) & 0x7F;
        const size_t n = scan.length(w + pos, nw - pos);
        if ((cmd == 0x50 || cmd == 0x51) && n > 1) {
            std::vector<uint8_t> gif(part.begin() + (pos + 1) * 4, part.begin() + (pos + n) * 4);
            scan_gif(gif, finish, signal);
        }
        pos += n;
    }
}

struct DmaEngine {
    std::vector<uint8_t> vif;  // the VIF1 transfer in flight: what the DMA has read and VIF1 not done
    size_t vif_pos = 0;        // in words
    VifScan scan;
    Chain vif_chain;           // a source chain is read a tag at a time, as VIF1 takes it: a stalled
    bool vif_chain_live = false;  // VIF1 stalls the DMA, and the game finishes the packets after it
    bool vif_chain_paused = false;  // the DMA stopped after a tag with IRQ, until STR again
    bool vif_irq_raised = false;    // and its handler has run
    bool vif_mpg = false;      // the transfer uploads a VU1 microprogram (kept for capture windows)
    bool vif_busy = false, vif_stalled = false;
    Chain gif;  // the GIF chain in flight
    bool gif_busy = false, gif_stopped = false;
    bool running = false;
} g_dma;

uint32_t &chcr_of(int channel) { return guest_ref<uint32_t>(guest_ref<uint32_t>(kDmaChanTable + 4 * channel)); }
uint32_t &tadr_of(int channel) { return guest_ref<uint32_t>(guest_ref<uint32_t>(kDmaChanTable + 4 * channel) + 0x30); }

// GT4_DMA_TRACE=N: the first N handshakes (debugging)
void dma_trace(const char *what, uint32_t value) {
    static long left = getenv("GT4_DMA_TRACE") ? atol(getenv("GT4_DMA_TRACE")) : 0;
    if (left <= 0) return;
    --left;
    const uint32_t s = kSprState + 0x50;
    log_info("dma: %s %08x -> counters %d %d %d, FBRST %u, D2 STR %u (VBlank %u)", what, value,
             guest_ref<int8_t>(s + 0x16), guest_ref<int8_t>(s + 0x17), guest_ref<int8_t>(s + 0x18),
             guest_ref<uint32_t>(kVif1Fbrst), (chcr_of(2) >> 8) & 1, kernel_vblank_count());
}

// After a handler ran: what it restarted.
void dma_after_handler() {
    if (g_dma.vif_chain_paused && (chcr_of(1) & 0x100)) {  // VIF1's chain goes on from D1_TADR
        g_dma.vif_chain_paused = false;
        g_dma.vif_irq_raised = false;
        g_dma.vif_chain.tadr = tadr_of(1) & 0x7FFFFFFF;
        g_dma.vif_chain.spr_tag = (tadr_of(1) & 0x80000000) != 0;
    }
    if (g_dma.vif_stalled && guest_ref<uint32_t>(kVif1Fbrst) == 8) {
        g_dma.vif_stalled = false;
        guest_ref<uint32_t>(kVif1Stat) &= ~0xC00u;  // STC clears INT and VIS
    }
    if (chcr_of(2) & 0x100) {
        if (g_dma.gif_busy && g_dma.gif_stopped) {
            g_dma.gif_stopped = false;
        } else if (!g_dma.gif_busy) {  // started by the handler: the chain at D2_TADR
            g_dma.gif = Chain();
            g_dma.gif.tadr = tadr_of(2) & 0x7FFFFFFF;
            g_dma.gif.spr_tag = (tadr_of(2) & 0x80000000) != 0;
            g_dma.gif.tte = (chcr_of(2) & 0x40) != 0;
            g_dma.gif_busy = true;
            g_dma.gif_stopped = false;
        }
    }
}

void dma_run() {
    if (g_dma.running) return;  // a handler restarting a stream: the loop below goes on with it
    g_dma.running = true;
    for (int guard = 0; guard < 1 << 20; ++guard) {
        if (g_dma.gif_busy && !g_dma.gif_stopped) {  // PATH3 first: textures as early as allowed
            std::vector<uint8_t> out;
            const int r = walk(g_dma.gif, out, (chcr_of(2) & 0x80) != 0);  // CHCR.TIE
            send_out(2, out);
            uint32_t &chcr = chcr_of(2);
            chcr = (chcr & 0xFFFF) | (g_dma.gif.last_tag & 0xFFFF0000u);
            tadr_of(2) = g_dma.gif.tadr;
            if (r == 1) {  // stopped at an IRQ tag: the handler says whether it goes on
                chcr &= ~0x100u;
                g_dma.gif_stopped = true;
                guest_ref<uint32_t>(kVif1Fbrst) = 0;
                if (!kernel_interrupt_now(true, 2)) chcr |= 0x100u;  // masked: it goes on
                dma_trace("GIF tag", g_dma.gif.last_tag);
                dma_after_handler();
            } else {
                g_dma.gif_busy = false;
                g_dma.gif_stopped = false;
                end_transfer(2);
            }
            continue;
        }
        if (g_dma.vif_busy && !g_dma.vif_stalled && !g_dma.vif_irq_raised) {
            auto fetch = [] {  // the DMA reads the next tag of the chain
                if (!g_dma.vif_chain_live || g_dma.vif_chain_paused) return false;
                g_chain_has_mpg = false;
                const int r = walk(g_dma.vif_chain, g_dma.vif, (chcr_of(1) & 0x80) != 0, true);
                if (r == 1) g_dma.vif_chain_paused = true;  // after a tag with IRQ (CHCR.TIE)
                else if (r != 2) g_dma.vif_chain_live = false;
                g_dma.vif_mpg |= g_chain_has_mpg;
                return true;
            };
            size_t pos = g_dma.vif_pos;
            uint32_t code = 0;
            bool stop = false;
            for (;;) {
                const uint32_t *w = reinterpret_cast<const uint32_t *>(g_dma.vif.data());
                const size_t nw = g_dma.vif.size() / 4;
                if (pos >= nw) {
                    if (fetch()) continue;
                    break;
                }
                code = w[pos];
                const size_t n = g_dma.scan.length(w + pos, SIZE_MAX);
                if (pos + n > nw && fetch()) continue;  // the code's data is in the next tags
                pos = std::min(pos + n, nw);
                if ((code >> 31) != 0) {
                    stop = true;
                    break;
                }
            }
            std::vector<uint8_t> part(g_dma.vif.begin() + g_dma.vif_pos * 4, g_dma.vif.begin() + pos * 4);
            g_dma.vif.erase(g_dma.vif.begin(), g_dma.vif.begin() + pos * 4);
            g_dma.vif_pos = 0;
            if (g_dma.vif_mpg) link_remember(GT4_REC_VIF1, part.data(), static_cast<uint32_t>(part.size()));
            send_out(1, part);
            {  // the GS events of the DIRECT packets (a live host reports SIGNAL itself, with its value)
                bool finish = false, signal = false;
                scan_vif_direct(part, finish, signal);
                if (!link_connected()) raise_gs_events(finish, signal);
                else if (finish) link_local_finish();
            }
            if (stop) {  // the interrupt bit: VIF1 stalls after this code until the handler resumes it
                g_dma.vif_stalled = true;
                guest_ref<uint32_t>(kVif1Stat) |= 0xC00u;  // INT, VIS
                guest_ref<uint32_t>(kVif1Code) = code;
                guest_ref<uint32_t>(kVif1Fbrst) = 0;
                if (!kernel_interrupt_now(false, 5)) guest_ref<uint32_t>(kVif1Fbrst) = 8;  // masked: no stall
                dma_trace("VIF1 code", code);
                dma_after_handler();
            } else if (g_dma.vif_chain_live && g_dma.vif_chain_paused) {
                // the DMA stopped after a tag with IRQ and VIF1 has done its data: the handler
                // (004AB6D8) counts it; with the GS FINISH too a GS -> EE download starts (004AB930),
                // and 004AB9C0 restarts the chain afterwards (D1 STR, from D1_TADR)
                uint32_t &chcr = chcr_of(1);
                chcr = ((chcr & 0xFFFF) | (g_dma.vif_chain.last_tag & 0xFFFF0000u)) & ~0x100u;
                tadr_of(1) = g_dma.vif_chain.tadr;
                g_dma.vif_irq_raised = true;
                kernel_interrupt_now(true, 1);
                dma_trace("VIF1 tag", g_dma.vif_chain.last_tag);
                dma_after_handler();
            } else {
                g_dma.vif_busy = false;
                g_dma.vif.clear();
                end_transfer(1);
            }
            continue;
        }
        break;
    }
    g_dma.running = false;
}

// After interrupts delivered asynchronously (the GS FINISH of a download, its DMA end): a handler
// may have restarted a stream.
void dma_poll() {
    if (g_dma.running) return;
    dma_after_handler();
    dma_run();
}

// A VIF1 transfer starts: its data (normal mode), or a source chain read as VIF1 goes.
void vif1_begin() {
    if (g_dma.vif_busy) log_warn("gs: VIF1 started while a transfer is in flight (stalled at word %zu)", g_dma.vif_pos);
    g_dma.vif.clear();
    g_dma.vif_pos = 0;
    g_dma.scan = VifScan();
    g_dma.vif_chain_live = false;
    g_dma.vif_chain_paused = false;
    g_dma.vif_irq_raised = false;
    g_dma.vif_mpg = false;
    g_dma.vif_busy = true;
    g_dma.vif_stalled = false;
}

void vif1_start(std::vector<uint8_t> &out) {
    vif1_begin();
    g_dma.vif.swap(out);
    dma_run();
}

void vif1_start_chain(uint32_t address, bool spr, bool tte) {
    vif1_begin();
    g_dma.vif_chain = Chain();
    g_dma.vif_chain.tadr = address;
    g_dma.vif_chain.spr_tag = spr;
    g_dma.vif_chain.tte = tte;
    g_dma.vif_chain_live = true;
    dma_run();
}

extern "C" void gt4_replace_func_004A1140(int channel, uint32_t address, int qwc) {
    reinterpret_cast<WaitFn>(guest_ptr(0x004A0F68))(channel);  // waits until the channel is free
    KernelScope ks;  // the DMA-end interrupt is delivered when this returns
    start_flag(channel);
    std::vector<uint8_t> out;
    append(out, mem(address, false), static_cast<uint32_t>(qwc));
    if (channel == 1) vif1_start(out);
    else finish_transfer(channel, out);
}

extern "C" void gt4_replace_func_004ABA50(int channel, uint32_t address) {
    reinterpret_cast<WaitFn>(guest_ptr(0x004A0F68))(channel);
    KernelScope ks;
    start_flag(channel);
    const uint32_t regs = guest_ref<uint32_t>(kDmaChanTable + 4 * channel);
    // the registers as the original sets them: QWC 0, TADR (scratchpad: bit 31), CHCR source chain
    // started, TTE/TIE kept (its handler reads the mode and the last tag)
    // (the walk keeps the address as given: natively a chain can be on a host thread's stack)
    const bool spr_start = (address & 0x70000000) == 0x70000000;
    guest_ref<uint32_t>(regs + 0x20) = 0;
    guest_ref<uint32_t>(regs + 0x30) = spr_start ? (address & 0x3FF0) | 0x80000000u : address;
    guest_ref<uint32_t>(regs) = (guest_ref<uint32_t>(regs) & 0xFEC0) | 0x10000105;
    const uint32_t chcr = guest_ref<uint32_t>(regs);
    if (channel == 2) {  // GIF: stops at its IRQ tags (CHCR.TIE) and goes on in step with VIF1
        g_dma.gif = Chain();
        g_dma.gif.tadr = address;
        g_dma.gif.spr_tag = spr_start;
        g_dma.gif.tte = (chcr & 0x40) != 0;
        g_dma.gif_busy = true;
        g_dma.gif_stopped = false;
        dma_run();
        return;
    }
    if (channel == 1) {
        vif1_start_chain(address, spr_start, (chcr & 0x40) != 0);
        return;
    }
    std::vector<uint8_t> out;
    walk_chain(address, (chcr & 0x40) != 0, out);
    finish_transfer(channel, out);
}

// 0049FF38: resets VIF0, VIF1 (FBRST), both VUs (FBRST through vi28) and the GIF, and sets the
// VIFs' error masks. Nothing to reset natively; the register values are kept for readers.
// 004AB930: starts the next GS -> EE download of the list its DMA channel 1 TADR points at
// ({qwc, madr, id} entries; id 1 = last): VIF1_STAT.FDR and BUSDIR = 1, channel 1 towards memory.
// Here the transfer is done at once (the host's GS_IMAGE, or zeros headless) and the channel ends
// with its interrupt, whose handler (004AB6D8) goes on with the list or finishes (004AB9C0).
extern "C" void gt4_replace_func_004AB930(uint32_t state) {
    const uint32_t chcr = guest_ref<uint32_t>(0x10009000);
    const uint32_t tadr = guest_ref<uint32_t>(0x10009030);
    const uint32_t tag = (tadr & 0x0FFFFFFF) | 0x20000000;
    const uint32_t qwc = guest_ref<uint32_t>(tag), madr = guest_ref<uint32_t>(tag + 4), id = guest_ref<uint32_t>(tag + 8);
    guest_ref<uint32_t>(state + 0x44) = tag;
    guest_ref<uint32_t>(0x10003C00) = 0x800000;  // VIF1_STAT.FDR
    guest_ref<uint64_t>(0x12001040) = 1;         // BUSDIR: GS -> EE
    guest_ref<uint32_t>(0x10009010) = madr;
    guest_ref<uint32_t>(0x10009020) = qwc;
    guest_ref<uint32_t>(0x10009030) = tadr + 16;
    guest_ref<uint32_t>(0x10009000) = (chcr & ~0x10Fu) | 0x100;
    if (id != 1) guest_ref<uint8_t>(state + 0x15) = 0;
    std::vector<uint8_t> image(static_cast<size_t>(qwc) * 16);
    link_read_image(qwc, image.data());
    for (uint32_t q = 0; q < qwc; ++q) std::memcpy(mem(madr + q * 16, (madr & 0x80000000) != 0), &image[q * 16], 16);
    guest_ref<uint32_t>(0x10009010) = madr + qwc * 16;
    guest_ref<uint32_t>(0x10009020) = 0;
    guest_ref<uint32_t>(0x10009000) &= ~0x100u;
    kernel_raise_dmac(1);
}

extern "C" void gt4_replace_func_0049FF38() {
    guest_ref<uint32_t>(0x10003810) = 1;  // VIF0_FBRST
    guest_ref<uint32_t>(0x10003C10) = 1;  // VIF1_FBRST
    guest_ref<uint32_t>(0x10003000) = 1;  // GIF_CTRL
    guest_ref<uint32_t>(0x10003010) = 4;  // GIF_MODE
    guest_ref<uint32_t>(0x10003820) = 6;  // VIF0_ERR
    guest_ref<uint32_t>(0x10003C20) = 6;  // VIF1_ERR
}

// 004AB430: the graphics library's VBlank (INTC 2) handler (no matched source; docs/IOP_SERVICES.md
// section 4). State S = scratchpad 0x70002050: +0 u64 VBlank counter, +0x10 optional hook
// fn(counter), +0x14 u8 CSR.FIELD, +0x40 list of VSync waiters {next, thread id}, +0x20/+0x2C/+0x38
// the DMA channels' waiter lists, +0x48/+0x50 accumulated performance counters (0 here).
extern "C" int gt4_sys_WakeupThread(int);
extern "C" int gt4_replace_func_004AB430(int) {
    const uint32_t s = 0x70002050;
    guest_ref<uint8_t>(s + 0x14) = (guest_ref<uint32_t>(kGsCsr) >> 13) & 1;
    const uint64_t counter = ++guest_ref<uint64_t>(s);
    if (const uint32_t hook = guest_ref<uint32_t>(s + 0x10))
        reinterpret_cast<void (*)(int)>(guest_ptr(hook))(static_cast<int>(counter));
    auto wake_list = [](uint32_t head_addr) {
        for (uint32_t node = guest_ref<uint32_t>(head_addr); node; node = guest_ref<uint32_t>(node))
            gt4_sys_WakeupThread(static_cast<int>(guest_ref<uint32_t>(node + 4)));
    };
    wake_list(s + 0x40);
    for (int ch = 0; ch < 3; ++ch) wake_list(s + 0x20 + 0xC * ch);
    return 1;
}

namespace gt4::rt {

void gs_init() {
    kernel_set_vblank_hook(on_vblank);
    kernel_set_interrupt_hook(dma_poll);
    guest_ref<uint32_t>(kGsCsr) = 0;
    guest_ref<uint64_t>(kGsPriv + 0x1010) = 0xFF00;  // IMR: every GS interrupt masked (the kernel's reset value)
}

}  // namespace gt4::rt
