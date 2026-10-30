#pragma once
// GT4_GS_WRITERS (debugging aid, runtime/gs_writers.cpp): which game functions wrote the bytes that the DMA
// channels deliver towards the GS (VIF1 codes and batches, DIRECT and PATH3 GIF packets, DMA tags), over
// windows of VBlanks. docs/GRAPHICS_API_PLAN.md ("Measuring") says how to read its reports.
#include <cstdint>

namespace gt4::rt::gsw {

extern bool g_on;  // GT4_GS_WRITERS is set: runtime/gs.cpp feeds what the channels deliver

void init();                  // from gs_init: reads the variable, installs the write handler
void vblank(uint32_t count);  // the interrupt thread, at every VBlank: opens and closes the windows
// 004A1140 / 004ABA50 started a transfer on a channel (0 VIF0, 1 VIF1, 2 GIF); the return address and frame
// of the replacement name the game function that kicked it (null: the DMA handler restarted a chain)
void kick(int channel, const void *return_address, const void *frame);
void tag(int channel, const uint8_t *tag);  // a DMA tag the channel read (its lower half: the chain)
// bytes the channel delivered, in order, from guest memory (by_ref: a ref/refe/refs tag's data, which lives
// elsewhere than the chain)
void data(int channel, const uint8_t *p, uint32_t bytes, bool by_ref);
bool active();  // a window is open: guest pages may be write-protected (iop.cpp reads the disc via a copy)

}  // namespace gt4::rt::gsw
