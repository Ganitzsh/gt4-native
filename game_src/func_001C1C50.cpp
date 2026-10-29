/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001C1C50 (matched with MMI asm): dst = a XOR b, byte by byte, over the image (rows padded to
 * 16 bytes) rounded up to 128 bytes. */
typedef unsigned int u128 __attribute__((mode(TI)));
extern "C" void func_001C1C50(u128 *dst, u128 *a, u128 *b, int width, int height) {
    const int bytes = ((((width + 15) & ~15) * height) + 127) & ~127;
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (int i = 0; i < bytes; ++i) d[i] = x[i] ^ y[i];
}
