#pragma once
// The game side of the link to gt4_host (include/gt4/link/protocol.h): records to the host,
// pad state and GS events from it. Without --link the records can go to a capture file (--capture)
// for gt4_host --replay, or nowhere.
#include "gt4/link/protocol.h"

#include <cstdint>
#include <vector>

namespace gt4::rt {

bool link_open(const char *name);          // --link: the section gt4_host created
bool link_capture(const char *path);       // --capture: write the game->host records to a file
bool link_active();
bool link_capture_window();
bool link_capture_finished();  // a bounded capture window (GT4_CAPTURE_FRAMES) has closed
void link_capture_close();
// Keeps a record for the start of a capture window (state sent once that a replay needs: the VU1
// microprograms).
void link_remember(uint32_t type, const void *payload, uint32_t size);               // the capture is writing now (GT4_CAPTURE_FROM / _FRAMES)
bool link_connected();                    // a live host (--link), which reports GS events itself

// Sends one record (thread-safe). `extra` (optional) follows `payload` in the same record.
void link_send(uint32_t type, const void *payload, uint32_t size, const void *extra = nullptr,
               uint32_t extra_size = 0);

// While one is open on a thread, its GT4_REC_GFX and GT4_REC_GFX_DATA records reach the host in batches (link.cpp's
// g_head: by size and count, or when another kind of record goes, or before a wait for room); closing the last one
// publishes the rest. For the frame walk (runtime/frame.cpp), whose records are many and small. The rule to keep:
// publish before any wait on the host. Nothing inside a batch may wait for an answer to a record still held back
// (today the walk's only waits are link_read_image, after its GS_READ, which publishes, and link_send's wait for
// ring room, which publishes first).
struct LinkBatch {
    LinkBatch();
    ~LinkBatch();
    LinkBatch(const LinkBatch &) = delete;
    LinkBatch &operator=(const LinkBatch &) = delete;
};
// The QPC ticks the calling thread has spent in link_send waiting for room in the ring, since its start.
int64_t link_send_wait_ticks();
// Records sent to the host and the publishes that made them visible, since the start.
struct LinkSendCounts {
    uint64_t records, publishes;
};
LinkSendCounts link_send_counts();

// Host -> game records, polled at each frame by the kernel's frame clock.
void link_poll();

// A read of graphics memory (runtime/frame.cpp, a read record): sends GS_READ {id, qwc} and waits (2 s at most) for the host's
// GS_IMAGE, copied to dst (qwc x 16 bytes). Without a host, or on timeout, dst is zero-filled and
// false is returned.
bool link_read_image(uint32_t qwc, void *dst);
// The same without waiting (gfx::kReadAsync, the PS2's interrupt-driven downloads): the pixels land in a buffer of
// the runtime when the host's GS_IMAGE arrives and reach dst through link_async_deliver. Never waits (a host far
// behind: the read sent untracked). The first of all is waited for, but pgl sends that one as an ordinary read
// (link_read_wait_first, below); this function's own first-read branch is a fallback for any other caller.
bool link_read_image_async(uint32_t qwc, void *dst);
// For a read the game would make asynchronous (runtime/native/pgl.cpp, when it builds the read record): true when it is
// to be waited for after all, so its record goes as an ordinary read (no gfx::kReadAsync) and the host answers it at
// once. That is one larger than the landing buffers, and the first of all (nothing has landed yet for the game to read
// the frame after), which this call takes: the reads after it are asynchronous, as link_read_image_async treats them.
bool link_read_wait_first(uint32_t qwc);
// Its pixels go into dst only here: the newest that landed for dst, if any (called by the code that owns dst, when
// it runs again: runtime/native/pgl.cpp, 00497F48 and 00497EF8).
void link_async_deliver(void *dst);
// The last resort against a host that falls behind: from a game thread at a safe point (no lock held, not in an
// interrupt), waits while the game is more than a few VBlanks ahead of the newest frame the host drew.
void link_wait_for_host();

// The host lost a texture content it was counted on to keep (GT4_REC_HOST_CMD 5): runtime/frame.cpp sends every
// content again.
void frame_forget_sent_contents();

// The pad state the host sent (18 bytes, libpad2 layout); false if none yet.
bool link_pad(int port, uint8_t out[18]);
// The wheel the host sent (GT4_REC_WHEEL index 0, PLAN.md D7): its newest state; false while none is connected.
bool link_wheel(Gt4LinkWheel &out);

// The settings pages (docs/modern/MENU_DESIGN.md 3.6). The host's GT4_REC_SETTINGS payloads, oldest first, copied into
// `out`; false when none is waiting. They are kept in a fixed 256 KiB buffer (link_poll may not allocate); one that
// does not fit is dropped and logged.
bool link_take_settings(std::vector<uint8_t> &out);
// GT4_REC_HOST_CMD 6 (open the settings) or 7 (close them) received since the last call, the newest; 0 when none.
uint32_t link_take_menu_command();

}  // namespace gt4::rt
