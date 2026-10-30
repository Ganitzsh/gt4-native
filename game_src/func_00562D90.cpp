/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 00562D90: the original restarts the IPU-out (3) and IPU-in (4) DMA channels from a saved state
   (00562E90's), skipping the IPU input already queued. Nothing calls it: no call, address or data word in the image
   refers to it (it sits among the movie decoder's IPU helpers, all under the movie loop 0054E3D8 that
   runtime/native/movie.cpp replaces); if it is ever reached, gt4_not_native stops the game with its caller
   (runtime/missing.cpp). */
extern "C" void func_00562D90() {
    gt4_not_native(0x00562D90, __builtin_return_address(0),
                   "IPU path not used natively: no caller in the image (a movie decoder helper)");
}
