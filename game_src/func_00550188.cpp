/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00550188: the original is the IPU input ring's refill: suspends the IPU-in channel (4), queues the
   newly filled blocks as DMA tags and restarts it. Not run natively: its only callers are under the movie loop
   0054E3D8 (0054E3D8 -> 0054F7E0 (its address is taken there)), which runtime/native/movie.cpp replaces with an FFmpeg
   player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is ever reached,
   gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" void func_00550188() {
    gt4_not_native(0x00550188, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 (its address is taken there))");
}
