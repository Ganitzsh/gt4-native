/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 005504C8: the original queues the next stretch of the IPU input ring and restarts the IPU-out (3) and
   IPU-in (4) DMA channels, IPU_CTRL. Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 ->
   0054F7E0 (its address is taken there)), which runtime/native/movie.cpp replaces with an FFmpeg player, so nothing
   here drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops the game
   with its caller (runtime/missing.cpp). */
extern "C" void func_005504C8() {
    gt4_not_native(0x005504C8, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 (its address is taken there))");
}
