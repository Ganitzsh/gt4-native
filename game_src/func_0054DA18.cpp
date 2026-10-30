/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 0054DA18: the original arms the IPU-out transfer: busy state 5, enables the IPU interrupt,
   acknowledges and unmasks D_STAT channel 3, D_ENABLEW = 0. Not run natively: its only callers are under the movie
   loop 0054E3D8 (0054E3D8 -> 0054F7E0 -> 0054FA48 -> 00562A70 -> 00562B20), which runtime/native/movie.cpp replaces
   with an FFmpeg player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is ever
   reached, gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" void func_0054DA18() {
    gt4_not_native(0x0054DA18, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA48 -> 00562A70 -> 00562B20)");
}
