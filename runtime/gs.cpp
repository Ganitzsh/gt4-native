// The display side of the graphics boundary (docs/HW_ACCESS.md, docs/IOP_SERVICES.md section 4).
//
// What the game draws reaches the host as native records (runtime/frame.cpp, runtime/native/pgl.cpp). Here: the
// display the graphics library sets (gt4_gfx_display: which buffer the screen shows), sent to the host before a
// VBlank when it changed, then a frame marker; the field of the interlaced NTSC display, which alternates every
// VBlank; and the graphics library's VBlank handler (004AB430), native.
#include "audio.h"
#include "guest.h"
#include "gt4/gfx.h"
#include "gt4/link/protocol.h"
#include "kernel.h"
#include "link.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

using namespace gt4::rt;
namespace gfx = gt4::gfx;

namespace {

uint32_t g_field = 0;
// The display in the library's encoding (PMODE, SMODE2, DISPFB1, DISPLAY1, DISPFB2, DISPLAY2, BGCOLOR), as game
// threads last set it; read at the VBlank without a lock, as the PS2 read its registers (a display set halfway is
// sent whole at the next VBlank).
volatile uint64_t g_display[7] = {0, 0, 0, 0, 0, 0, 0};
gfx::DisplayRecord g_display_sent;
bool g_display_valid = false;

void on_vblank(uint32_t count) {
    g_field ^= 1;   // the field alternates every VBlank on an interlaced display
    audio_vblank();
    if (link_capture_finished() && !link_connected()) {  // a standalone capture run ends with its window
        link_capture_close();
        log_info("capture window closed at VBlank %u: exiting", count);
        ExitProcess(0);
    }
    if (!link_active()) return;
    static bool capturing = false;
    if (link_capture_window() != capturing) {  // a capture window opens: send the display again
        capturing = !capturing;
        g_display_valid = false;
    }
    const gfx::DisplayRecord d = gfx::displayFromRegisters(g_display[0], g_display[1], g_display[2], g_display[3], g_display[4],
                                                           g_display[5], g_display[6]);
    if (!g_display_valid || !(d == g_display_sent)) {
        g_display_valid = true;
        g_display_sent = d;
        link_send(GT4_REC_DISPLAY, &d, sizeof d);
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
// fn(counter), +0x14 u8 the field (CSR.FIELD), +0x40 list of VSync waiters {next, thread id}, +0x20/+0x2C/+0x38
// the DMA channels' waiter lists, +0x48/+0x50 accumulated performance counters (0 here).
extern "C" int gt4_sys_WakeupThread(int);
extern "C" int gt4_replace_func_004AB430(int) {
    const uint32_t s = 0x70002050;
    guest_ref<uint8_t>(s + 0x14) = static_cast<uint8_t>(g_field);
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

// The display from the next VBlank on, in the graphics library's encoding (compat/game/gt4_gfx.h): bit k of
// `which` gives values[k] (PMODE, SMODE2, DISPFB1, DISPLAY1, DISPFB2, DISPLAY2, BGCOLOR).
extern "C" void gt4_gfx_display(uint32_t which, const uint64_t *values) {
    for (int k = 0; k < 7; ++k)
        if (which >> k & 1) g_display[k] = values[k];
}

namespace gt4::rt {

void watch_write_init();  // watch_write.cpp
void hw_audit_init();     // hw_audit.cpp

void gs_init() {
    watch_write_init();  // GT4_WATCH_WRITE (debugging)
    kernel_set_vblank_hook(on_vblank);
    hw_audit_init();     // GT4_HW_AUDIT (checking the port)
}

}  // namespace gt4::rt
