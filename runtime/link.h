#pragma once
// The game side of the link to gt4_host (include/gt4/link/protocol.h): records to the host,
// pad state and GS events from it. Without --link the records can go to a capture file (--capture)
// for gt4_host --replay, or nowhere.
#include <cstdint>

namespace gt4::rt {

bool link_open(const char *name);          // --link: the section gt4_host created
bool link_capture(const char *path);       // --capture: write the game->host records to a file
bool link_active();

// Sends one record (thread-safe). `extra` (optional) follows `payload` in the same record.
void link_send(uint32_t type, const void *payload, uint32_t size, const void *extra = nullptr,
               uint32_t extra_size = 0);

// Host -> game records, polled at each VBlank by the kernel's interrupt thread.
void link_poll();

// The pad state the host sent (18 bytes, libpad2 layout); false if none yet.
bool link_pad(int port, uint8_t out[18]);

}  // namespace gt4::rt
