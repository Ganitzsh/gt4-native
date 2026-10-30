/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0048D530 (recovered matched source with VU0 asm): out = a * b, quaternions with w last.
 * As the original forms it: c = (a x b, -a.x b.x - a.y b.y - a.z b.z - a.w b.w) (the w lane on the FPU), then
 * out = (a * b.w + b * a.w) + c lane by lane, so out.w = a.w b.w - (a . b). */
extern "C" void func_0048D530(float *out, const float *a, const float *b) {
    float c[4];
    c[3] = -a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    c[0] = a[1] * b[2] - b[1] * a[2];
    c[1] = a[2] * b[0] - b[2] * a[0];
    c[2] = a[0] * b[1] - b[0] * a[1];
    float r[4];
    for (int k = 0; k < 4; ++k) {
        const float s = a[k] * b[3] + b[k] * a[3];
        r[k] = s + c[k];
    }
    for (int k = 0; k < 4; ++k) out[k] = r[k];
}
