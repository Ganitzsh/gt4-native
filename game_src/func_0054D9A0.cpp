/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 0054D9A0: the original is the IPU-out DMA (channel 3) end handler: clears the busy flag, signals the
   decoder's semaphore and acknowledges D_STAT. Not run natively: its only callers are under the movie loop 0054E3D8
   (0054E3D8 -> 0054F7E0 -> 0054D638 (registers it)), which runtime/native/movie.cpp replaces with an FFmpeg player, so
   nothing here drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops
   the game with its caller (runtime/missing.cpp). */
extern "C" void func_0054D9A0() {
    gt4_not_native(0x0054D9A0, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054D638 (registers it))");
}
