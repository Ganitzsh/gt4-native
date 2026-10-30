/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0048D4C0 (recovered matched source with VU0 asm: VOPMULA / VOPMSUB): out.xyz = a x b, formed
 * as the VU forms it (a.y b.z - b.y a.z, ...); out.w = a.w (the lane the cross product leaves). */
extern "C" void func_0048D4C0(float *out, const float *a, const float *b) {
    const float x = a[1] * b[2] - b[1] * a[2];
    const float y = a[2] * b[0] - b[2] * a[0];
    const float z = a[0] * b[1] - b[0] * a[1];
    const float w = a[3];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}
