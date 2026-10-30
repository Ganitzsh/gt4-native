/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0048DF98 (recovered matched source with VU0 asm: vector add on VU0): out[i] = a[i] + b[i]
 * (all four lanes) for arrays of 16-byte vectors. */
extern "C" void func_0048DF98(float (*out)[4], int i, const float (*a)[4], const float (*b)[4]) {
    for (int k = 0; k < 4; ++k) out[i][k] = a[i][k] + b[i][k];
}
