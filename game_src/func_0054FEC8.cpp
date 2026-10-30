/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 0054FEC8: the original builds the DMA tag ring over the IPU input buffer and starts the IPU-in
   channel (4) on it in chain mode. Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 ->
   0054F7E0 -> 0054FA98), which runtime/native/movie.cpp replaces with an FFmpeg player, so nothing here drives the IPU
   or its DMA channels, which nothing models; if it is ever reached, gt4_not_native stops the game with its caller
   (runtime/missing.cpp). */
extern "C" void func_0054FEC8() {
    gt4_not_native(0x0054FEC8, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98)");
}
