/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0048D408 (recovered matched source with VU0 asm: vector sub on VU0): out = a - b, all four
 * lanes of 16-byte vectors. */
extern "C" void func_0048D408(float *out, const float *a, const float *b) {
    for (int k = 0; k < 4; ++k) out[k] = a[k] - b[k];
}
