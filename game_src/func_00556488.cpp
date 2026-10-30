/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00556488 (from its source): copies a 44-byte block out of an object with interrupts disabled
   (DIntr); re-enables them with gt4_interrupts_enable() instead of the `ei` instruction when they were enabled. */
struct Blob { char data[40]; int word; };
extern "C" int func_005B72A8();
extern "C" Blob *func_00556488(Blob *out, char *src) {
    int disabled = func_005B72A8();
    Blob b = *(Blob *)(src + 4);
    if (disabled) gt4_interrupts_enable();
    *out = b;
    return out;
}
