/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00562E90: the original saves the IPU DMA state: stops the IPU-in (4) and IPU-out (3) channels and
   reads their MADR/TADR/QWC/CHCR, IPU_BP and IPU_CTRL. Not run natively: its only callers are under the movie loop
   0054E3D8 (0054E3D8 -> 0054F7E0 -> 00550488), which runtime/native/movie.cpp replaces with an FFmpeg player, so
   nothing here drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops
   the game with its caller (runtime/missing.cpp). */
extern "C" void func_00562E90() {
    gt4_not_native(0x00562E90, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 00550488)");
}
