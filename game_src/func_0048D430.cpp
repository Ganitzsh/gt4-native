/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0048D430 (recovered matched source with VU0 asm: vector mul on VU0): out = a * b (per component), all four
 * lanes of 16-byte vectors. */
extern "C" void func_0048D430(float *out, const float *a, const float *b) {
    for (int k = 0; k < 4; ++k) out[k] = a[k] * b[k];
}
