/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stub of 004AB548: the original was the graphics library's VIF1 interrupt (INTC 5) handler: kept PATH3 texture
   streaming in step with VIF1's interrupt codes. Not run natively: an interrupt handler 004A03C0 installs; nothing
   raises VIF1 interrupts natively (the frame is sent whole by runtime/frame.cpp); if it is ever reached,
   gt4_not_native stops the game with its caller (runtime/missing.cpp). */
extern "C" int func_004AB548() {
    gt4_not_native(0x004AB548, __builtin_return_address(0), "an interrupt handler 004A03C0 installs; nothing raises VIF1 interrupts natively");
}
