/* Native version of 005AE770 (from its source): whether the caller runs in an interrupt handler, which the original
   tells by Status.IE being clear (mfc0 $12). Here gt4_status_ie() (gt4_prelude.h), which reads 1 as the translated
   mfc0 always did, so this returns 0, as before. */
extern "C" int func_005AE770(void) {
    return !gt4_status_ie();
}
