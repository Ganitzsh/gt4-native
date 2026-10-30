/* Native stub of 00568568: the original reads a macroblock address increment: FDEC peeks (00563198) for escapes, then
   IPU VDEC (0x30000000: MBAI). Not run natively: its only callers are under the movie loop 0054E3D8 (0054E3D8 ->
   0054F7E0 -> 0054FA98 -> 00563420 -> 00563F30 -> 00563F98), which runtime/native/movie.cpp replaces with an FFmpeg
   player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is ever reached,
   gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" void func_00568568() {
    gt4_not_native(0x00568568, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054FA98 -> 00563420 -> 00563F30 -> 00563F98)");
}
