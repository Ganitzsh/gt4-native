/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 004AB6D8: the original was the graphics library's DMA end handler for channels 0-2: chain messages
   (GS -> EE read steps, PATH3/VIF1 counters) and waking the channels' waiters. Not run natively: a DMAC handler
   004A0D98 installs; nothing raises DMA interrupts natively (the frame is sent whole by runtime/frame.cpp); if it
   is ever reached, gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" int func_004AB6D8() {
    gt4_not_native(0x004AB6D8, __builtin_return_address(0), "a DMAC handler 004A0D98 installs; nothing raises DMA interrupts natively");
}
