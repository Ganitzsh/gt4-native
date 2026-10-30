/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00562D20: the original aligns the IPU bit pointer to the next byte (IPU_BP, then FDEC through
   00563258). Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 -> 0054F7E0 -> 0054FA98 ->
   00563420 -> 005637A8 -> 00563868), which runtime/native/movie.cpp replaces with an FFmpeg player, so nothing here
   drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops the game with
   its caller (runtime/missing.cpp). */
extern "C" void func_00562D20() {
    gt4_not_native(0x00562D20, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 005637A8 -> 00563868)");
}
