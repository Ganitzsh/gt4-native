/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched; the sibling 001C1678, luminance, is the runtime replacement in
 * runtime/native/unit_001C0E78.cpp): RGBA32 rows to one 8-bit chroma byte per pixel,
 * ((r + 256) * 256 - 216 g - 24 b) >> 9 (= 128 + 0.5 r - 0.42 g - 0.047 b), from the original's
 * MMI words (r, g, b are bytes 0, 1, 2 of each pixel). Each of `rows` rows reads width / 4
 * quadwords (4 pixels each, lq: the source is read 16-aligned and is contiguous across rows) and
 * writes 4 bytes per quadword to dst, then zero-fills the rest of the destination row, whose
 * stride is width rounded up to 16. rows 0 does nothing (the loop is a do-while after that check). */
extern "C" void func_001C1760(int dst, int src, int width, int rows)
{
    unsigned int stride = (unsigned int)((width + 15) & -16);
    unsigned int n = (unsigned int)width >> 2;
    const unsigned char *s = (const unsigned char *)((unsigned int)src & ~15u);
    unsigned int row = (unsigned int)dst;
    if (rows == 0)
        return;
    do {
        unsigned int *o = (unsigned int *)row;
        for (unsigned int i = n; i != 0; i--) {
            unsigned int w = 0;
            for (int k = 0; k < 4; k++) {
                unsigned int r = s[4 * k], g = s[4 * k + 1], b = s[4 * k + 2];
                unsigned int v = (((r + 256) << 8) - 216 * g - 24 * b) >> 9;
                w |= (v & 0xFF) << (8 * k);
            }
            *o++ = w;
            s += 16;
        }
        row += stride;
        while ((unsigned int)o != row)
            *o++ = 0;
    } while (--rows != 0);
}
