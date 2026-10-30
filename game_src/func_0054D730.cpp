/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 0054D730: the original is the decoder's interrupt tear-down: acknowledges D_STAT, removes the two
   handlers 0054D638 added, clears D_PCR bit 3. Not run natively: its only callers are under the movie loop 0054E3D8
   (0054E3D8 -> 0054F7E0 -> 0054F9A8), which runtime/native/movie.cpp replaces with an FFmpeg player, so nothing here
   drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops the game with
   its caller (runtime/missing.cpp). */
extern "C" void func_0054D730() {
    gt4_not_native(0x0054D730, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054F9A8)");
}
