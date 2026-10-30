// The display side of the graphics boundary (docs/HW_ACCESS.md, docs/IOP_SERVICES.md section 4).
//
// What the game draws reaches the host as native records (runtime/frame.cpp, runtime/native/pgl.cpp). Here: at
// every VBlank the display registers the game wrote (PMODE .. BGCOLOR at 0x12000000) go to the host when they
// changed, then a frame marker; CSR.FIELD toggles as on an interlaced NTSC display; and the graphics library's
// VBlank handler (004AB430) runs natively.
#include "audio.h"
#include "guest.h"
#include "gt4/link/protocol.h"
#include "kernel.h"
#include "link.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

using namespace gt4::rt;

namespace {

constexpr uint32_t kGsPriv = 0x12000000;
constexpr uint32_t kGsCsr = 0x12001000;

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

// 004A1140(channel, address, qwc) and 004ABA50(channel, list) started a DMA transfer (normal mode, source chain):
// every caller is native now (the uploads, 004A11F8 the frame's kick, 004A12B8 the microprogram), so a call is a
// call nothing should make.
extern "C" void gt4_replace_func_004A1140(int channel, uint32_t address, int qwc) {
    log_error("gs: a DMA transfer started on channel %d (%u qwords at %08x): nothing sends it", channel, qwc, address);
}

extern "C" void gt4_replace_func_004ABA50(int channel, uint32_t address) {
    log_error("gs: a DMA chain started on channel %d (at %08x): nothing sends it", channel, address);
}

// 0049FF38: reset VIF0, VIF1, both VUs and the GIF and set the VIFs' error masks: nothing of them exists.
extern "C" void gt4_replace_func_0049FF38() {}

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

void watch_write_init();  // watch_write.cpp

void gs_init() {
    watch_write_init();  // GT4_WATCH_WRITE (debugging)
    kernel_set_vblank_hook(on_vblank);
    guest_ref<uint32_t>(kGsCsr) = 0;
    guest_ref<uint64_t>(kGsPriv + 0x1010) = 0xFF00;  // IMR: every GS interrupt masked (the kernel's reset value)
}

}  // namespace gt4::rt
