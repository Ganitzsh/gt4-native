/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 004AB9C0: the original ran a read's callback, then reset BUSDIR, VIF1 and DMA channels 1 and 2
   after a GS -> EE read. Not run natively: called only from the DMA end handler 004AB6D8, which nothing raises
   natively (reads are served by runtime/frame.cpp); if it is ever reached, gt4_not_native stops the game with its
   caller (runtime/missing.cpp). */
extern "C" void func_004AB9C0() {
    gt4_not_native(0x004AB9C0, __builtin_return_address(0), "called only from the DMA end handler 004AB6D8, which nothing raises natively");
}
