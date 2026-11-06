#pragma once
// The game side of the link to gt4_host (include/gt4/link/protocol.h): records to the host,
// pad state and GS events from it. Without --link the records can go to a capture file (--capture)
// for gt4_host --replay, or nowhere.
#include <cstdint>

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

// Host -> game records, polled at each frame by the kernel's frame clock.
void link_poll();

// A read of graphics memory (runtime/frame.cpp, a read record): sends GS_READ {id, qwc} and waits (2 s at most) for the host's
// GS_IMAGE, copied to dst (qwc x 16 bytes). Without a host, or on timeout, dst is zero-filled and
// false is returned.
bool link_read_image(uint32_t qwc, void *dst);
// The same without waiting (gfx::kReadAsync, the PS2's interrupt-driven downloads): the pixels land in a buffer of
// the runtime when the host's GS_IMAGE arrives and reach dst through link_async_deliver. Never waits (a host far
// behind: the read sent untracked); the first of all is read as link_read_image does.
bool link_read_image_async(uint32_t qwc, void *dst);
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

}  // namespace gt4::rt
