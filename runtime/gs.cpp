// The display side of the graphics boundary (docs/HW_ACCESS.md, docs/IOP_SERVICES.md section 4).
//
// What the game draws reaches the host as native records (runtime/frame.cpp, runtime/native/pgl.cpp). Here: the
// display the graphics library sets (gt4_gfx_display: which buffer the screen shows), sent to the host at the end
// of a frame when it changed, then a frame marker; the field GT4 renders (it draws one field of an interlaced
// picture a frame), which alternates every frame; and the graphics library's frame callback (004AB430, its VBlank
// handler on the PS2), native.
#include "audio.h"
#include "guest.h"
#include "gt4/gfx.h"
#include "gt4/link/protocol.h"
#include "kernel.h"
#include "link.h"
#include "load_stats.h"
#include "log.h"
#include "runtime.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

using namespace gt4::rt;
namespace gfx = gt4::gfx;

namespace {

uint32_t g_field = 0;
// The display in the library's encoding (PMODE, SMODE2, DISPFB1, DISPLAY1, DISPFB2, DISPLAY2, BGCOLOR), as game
// threads last set it; read at the frame's end without a lock (a display set halfway is sent whole at the next).
volatile uint64_t g_display[7] = {0, 0, 0, 0, 0, 0, 0};
gfx::DisplayRecord g_display_sent;
bool g_display_valid = false;

// A tick's link records: the display when it changed (again when a capture window opened or closed), then the VSync.
// From the clock's thread at the tick, or from a kick right after its FrameEnd (runtime/frame.cpp frame_tick).
void send_tick(const ClockTick &t) {
    static bool capturing = false;
    if (t.captureWindow != capturing) {  // a capture window opens: send the display again
        capturing = !capturing;
        g_display_valid = false;
    }
    const gfx::DisplayRecord d = gfx::displayFromRegisters(t.display[0], t.display[1], t.display[2], t.display[3], t.display[4],
                                                           t.display[5], t.display[6]);
    if (!g_display_valid || !(d == g_display_sent)) {
        g_display_valid = true;
        g_display_sent = d;
        link_send(GT4_REC_DISPLAY, &d, sizeof d);
    }
    const uint32_t rec[2] = {t.field, t.count};
    link_send(GT4_REC_VSYNC, rec, sizeof rec);
}

void on_frame(uint32_t count) {
    g_field ^= 1;   // the field alternates every frame
    load_stats_vblank(count);   // (GT4_LOAD_STATS: the loads timed, D10)
    audio_frame();
    if (link_capture_finished() && !link_connected()) {  // a standalone capture run ends with its window
        link_capture_close();
        log_info("capture window closed at frame %u: exiting", count);
        ExitProcess(0);
    }
    if (!link_active()) return;
    // the display as it stands now, the field and the capture window, sent now or (scene records on, a kick walking its
    // list) after that list's FrameEnd (docs/modern/CONTRACTS.md a.5.1)
    ClockTick t{};
    for (int k = 0; k < 7; ++k) t.display[k] = g_display[k];
    t.field = g_field;
    t.count = count;
    t.captureWindow = link_capture_window();
    frame_tick(t, send_tick);
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

// 004AB430: the graphics library's frame callback (its VBlank interrupt handler on the PS2, no matched source;
// 004A03C0 adds it to the runtime's frame clock). State S = scratchpad 0x70002050: +0 u64 frame counter, +0x10
// optional hook fn(counter), +0x14 u8 the field, +0x40 list of waiters for the next frame {next, thread id},
// +0x20/+0x2C/+0x38 the transfer channels' waiter lists, +0x48/+0x50 accumulated performance counters (0 here).
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
    kernel_set_frame_hook(on_frame);
    hw_audit_init();     // GT4_HW_AUDIT (checking the port)
}

}  // namespace gt4::rt
