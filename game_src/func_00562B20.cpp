/* compiler: ee-gcc2.96-nogcse */
/* Native stub of 00562B20: the original is the colour conversion of a decoded picture's macroblocks: IPU CSC from the
   IPU-in channel (4) into the IPU-out channel (3), with DMA and IPU waits. Not run natively: its only callers are
   under the movie loop 0054E3D8 (0054E3D8 -> 0054F7E0 -> 0054FA48 -> 00562A70), which runtime/native/movie.cpp
   replaces with an FFmpeg player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is
   ever reached, gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" void func_00562B20() {
    gt4_not_native(0x00562B20, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA48 -> 00562A70)");
}
