/* Native stub of 005684E0: the original sets the picture type in IPU_CTRL and issues IPU VDEC (0x34000000: macroblock
   type). Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 -> 0054F7E0 -> 0054FA98 ->
   00563420 -> 00563F30 -> 00563F98 -> 00564368), which runtime/native/movie.cpp replaces with an FFmpeg player, so
   nothing here drives the IPU or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops
   the game with its caller (runtime/missing.cpp). */
extern "C" void func_005684E0() {
    gt4_not_native(0x005684E0, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 00563F30 -> 00563F98 -> 00564368)");
}
