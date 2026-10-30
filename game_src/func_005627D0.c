/* Native stub of 005627D0: the original sets bits of D_PCR (DMA priority / COP control) and writes D_STAT; returns 1
   when other channels already hold D_PCR. Not run natively: its only callers are under the movie loop 0054E3D8
   (0054E3D8 -> 0054F7E0 -> 0054D638 (also 00562820, which nothing calls)), which runtime/native/movie.cpp replaces
   with an FFmpeg player, so nothing here drives the IPU or its DMA channels, which nothing models; if it is ever
   reached, gt4_not_native stops the game with its caller (runtime/missing.cpp). */
void func_005627D0(void) {
    gt4_not_native(0x005627D0, __builtin_return_address(0),
                   "IPU path not used natively: reached only from the movie loop 0054E3D8, which runtime/native/movie.cpp replaces (0054E3D8 -> 0054F7E0 -> 0054D638 (also 00562820, which nothing calls))");
}
