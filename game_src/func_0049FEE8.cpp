/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049FEE8 (matched): the original cleared the GS's VSync event (CSR = 8) and polled CSR until
 * the next one, at most 240,000 times, before a GS reset (004A03C0). There is no GS: nothing to wait for. */
extern "C" void func_0049FEE8(void) {}
