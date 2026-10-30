/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00568248: the original is the per-macroblock IPU BDEC (block decode) of the slice loop 00563F98, with
   the IPU-out DMA (channel 3) into the request's block buffer. Not run natively: its only callers are under the movie
   loop 0054E3D8 (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 00563F30 -> 00563F98), which runtime/native/movie.cpp
   replaces with an FFmpeg player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is
   ever reached, gt4_not_native stops the game with its caller (runtime/missing.cpp). */
void func_00568248(void) {
    gt4_not_native(0x00568248, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 00563F30 -> 00563F98)");
}
