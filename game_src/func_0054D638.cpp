/* Native stub of 0054D638: the original is the decoder's interrupt set-up: acknowledges the IPU DMA channels' D_STAT
   bits, adds the IPU interrupt handler 0054D960 (INTC 8) and the IPU-out DMA handler 0054D9A0 (channel 3), sets D_PCR
   (005627D0). Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 -> 0054F7E0), which
   runtime/native/movie.cpp replaces with an FFmpeg player, so nothing here drives the IPU or its DMA channels, which
   nothing models; if it is ever reached, gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" void func_0054D638() {
    gt4_not_native(0x0054D638, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0)");
}
