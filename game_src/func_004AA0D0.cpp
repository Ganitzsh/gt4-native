/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in for 004AA0D0 (no matched source yet): a colour from floats. r, g and b are
 * multiplied by `scale` ($f16), a by 128; each is clamped below at 0, rounded by + 0.5, clamped
 * above at 255 and truncated (cvt.w.s); the result is r | g << 8 | b << 16 | a << 24. Plain C in
 * place of the worker draft's max.s/min.s asm statements. */
static inline int channel(float v) {
    if (v < 0.0f) v = 0.0f;
    v += 0.5f;
    if (v > 255.0f) v = 255.0f;
    return (int)v;
}

extern "C" int func_004AA0D0(float r, float g, float b, float a, float scale) {
    return channel(r * scale) | channel(g * scale) << 8 | channel(b * scale) << 16 | channel(a * 128.0f) << 24;
}
