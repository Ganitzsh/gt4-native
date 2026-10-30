/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0048EB10 (recovered matched source with VU0 asm): out = m[0] v.x + m[1] v.y + m[2] v.z +
 * m[3] v.w, the 4-vector v through the four rows of m (all four lanes, summed in that order). */
extern "C" void func_0048EB10(const float (*m)[4], float *out, const float *v) {
    float r[4];
    for (int k = 0; k < 4; ++k) r[k] = m[0][k] * v[0] + m[1][k] * v[1] + m[2][k] * v[2] + m[3][k] * v[3];
    for (int k = 0; k < 4; ++k) out[k] = r[k];
}
