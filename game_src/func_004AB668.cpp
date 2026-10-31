/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 004AB668: the original was the graphics library's GS interrupt (INTC 0) handler: acknowledged FINISH
   and started the second half of a GS -> EE read (004AB930). Not run natively: an interrupt handler 004A03C0
   installs; nothing raises GS interrupts natively (reads are served by runtime/frame.cpp); if it is ever reached,
   gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" int func_004AB668() {
    gt4_not_native(0x004AB668, __builtin_return_address(0), "an interrupt handler 004A03C0 installs; nothing raises GS interrupts natively");
}
