// The graphics boundary on the game side (docs/HW_ACCESS.md, docs/IOP_SERVICES.md section 4).
//
// The game's graphics library starts every VIF0/VIF1/GIF transfer through two functions:
// 004A1140(channel, address, qwc) (normal mode) and 004ABA50(channel, tag address) (source chain).
// Their replacements do the transfer at once: walk the DMA chain in guest memory, send what the
// channel would have delivered to gt4_host (VIF1 stream, VIF0 stream, GIF PATH3), clear the
// channel's STR bit and raise its DMA-end interrupt so the game's handler (004AB6D8) advances.
// At every VBlank the GS privileged registers the game wrote are sent with a frame marker, and
// CSR.FIELD toggles as on an interlaced NTSC display.
#include "audio.h"
#include "guest.h"
#include "gt4/link/protocol.h"
#include "kernel.h"
#include "link.h"
#include "log.h"

#include <windows.h>

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

bool walk_chain(uint32_t tadr, bool tte, std::vector<uint8_t> &out, bool &irq_stop) {
    g_chain_has_mpg = false;
    uint32_t asr[2];
    int sp = 0;
    bool spr_tag = (tadr & 0x80000000) != 0;
    tadr &= 0x7FFFFFFF;
    for (int guard = 0; guard < 1 << 20; ++guard) {
        const uint8_t *tag = mem(tadr, spr_tag);
        uint64_t lo;
        std::memcpy(&lo, tag, 8);
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
            if (sp >= 2) return false;
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
        (void)irq;  // tag interrupts: the library sets TIE but its chains are not expected to use them
        (void)irq_stop;
        if (end) return true;
        tadr = next;
    }
    log_warn("gs: DMA chain did not end (1M tags)");
    return false;
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

void finish_transfer(int channel, std::vector<uint8_t> &out) {
    if (!out.empty()) {
        if (channel == 1) {
            if (g_chain_has_mpg) link_remember(GT4_REC_VIF1, out.data(), static_cast<uint32_t>(out.size()));
            g_chain_has_mpg = false;
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
    // the channel stops (STR clear) and interrupts at the end of the transfer
    const uint32_t regs = guest_ref<uint32_t>(kDmaChanTable + 4 * channel);
    guest_ref<uint32_t>(regs) &= ~0x100u;
    kernel_raise_dmac(channel);
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

extern "C" void gt4_replace_func_004A1140(int channel, uint32_t address, int qwc) {
    reinterpret_cast<WaitFn>(guest_ptr(0x004A0F68))(channel);  // waits until the channel is free
    KernelScope ks;  // the DMA-end interrupt is delivered when this returns
    start_flag(channel);
    std::vector<uint8_t> out;
    append(out, mem(address, false), static_cast<uint32_t>(qwc));
    finish_transfer(channel, out);
}

extern "C" void gt4_replace_func_004ABA50(int channel, uint32_t address) {
    reinterpret_cast<WaitFn>(guest_ptr(0x004A0F68))(channel);
    KernelScope ks;
    start_flag(channel);
    const uint32_t regs = guest_ref<uint32_t>(kDmaChanTable + 4 * channel);
    const uint32_t chcr = guest_ref<uint32_t>(regs);
    std::vector<uint8_t> out;
    bool irq = false;
    walk_chain(address, (chcr & 0x40) != 0, out, irq);
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
    guest_ref<uint32_t>(kGsCsr) = 0;
}

}  // namespace gt4::rt
