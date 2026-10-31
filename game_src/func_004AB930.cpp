/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 004AB930: the original started the DMA side of a GS -> EE read (VIF1 channel, BUSDIR). Not run
   natively: called only from the GS and DMA interrupt handlers 004AB668 and 004AB6D8, which nothing raises
   natively (reads are served by runtime/frame.cpp); if it is ever reached, gt4_not_native stops the game with its
   caller (runtime/missing.cpp). */
extern "C" void func_004AB930() {
    gt4_not_native(0x004AB930, __builtin_return_address(0), "called only from the GS and DMA interrupt handlers 004AB668 and 004AB6D8, which nothing raises natively");
}
