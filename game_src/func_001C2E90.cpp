/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001C2E90 (from its source): copies a 24-byte block out of an object with interrupts disabled
   (DIntr); re-enables them with gt4_interrupts_enable() instead of the `ei` instruction when they were enabled. */
struct Blob { char data[24]; };
extern "C" int func_005B72A8();
extern "C" Blob *func_001C2E90(Blob *out,char *src) {
 int disabled=func_005B72A8();
 Blob b=*(Blob *)(src+0x294);
 if(disabled) gt4_interrupts_enable();
 *out=b;
 return out;
}
