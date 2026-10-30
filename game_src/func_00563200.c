/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00563200: the original issues IPU FDEC without a shift: returns the next 24 bits of the bitstream (a
   start code). Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 -> 0054F7E0 -> 0054FA98
   -> 00563420 -> 005637A8 -> 00563868), which runtime/native/movie.cpp replaces with an FFmpeg player, so nothing here
   drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops the game with
   its caller (runtime/missing.cpp). */
void func_00563200(void) {
    gt4_not_native(0x00563200, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 005637A8 -> 00563868)");
}
