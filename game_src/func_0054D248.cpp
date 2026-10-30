/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 0054D248: the original issues IPU SETIQ (0x50000000: the intra quantiser matrix from the bitstream)
   and waits, refilling the input FIFO (0054D888). Not run natively: its only callers are under the movie loop 0054E3D8
   (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 005637A8 -> 005638B0), which runtime/native/movie.cpp replaces with
   an FFmpeg player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is ever reached,
   gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" void func_0054D248() {
    gt4_not_native(0x0054D248, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 005637A8 -> 005638B0)");
}
