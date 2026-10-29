/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001C1CA8 (matched with MMI asm): every byte negated (0 - b, modulo 256), over the image (rows
 * padded to 16 bytes) rounded up to 128 bytes. */
typedef unsigned int u128 __attribute__((mode(TI)));
extern "C" void func_001C1CA8(char *dst, u128 *src, int width, int height) {
    const int bytes = ((((width + 15) & ~15) * height) + 127) & ~127;
    const unsigned char *s = (const unsigned char *)src;
    for (int i = 0; i < bytes; ++i) dst[i] = (char)(unsigned char)(0 - s[i]);
}
