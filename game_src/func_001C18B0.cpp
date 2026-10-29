/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001C18B0 (matched with MMI asm): a 0/1 byte mask of an image, 1 where the source byte is above
 * `value` (its low byte), else 0; rows padded to 16 bytes. */
typedef unsigned int u128 __attribute__((mode(TI)));
extern "C" void func_001C18B0(u128 *dst, u128 *src, int width, int height, int value) {
    const unsigned char threshold = (unsigned char)value;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    const int bytes = ((width + 15) & ~15) * height;
    for (int i = 0; i < bytes; ++i) d[i] = s[i] > threshold ? 1 : 0;
}
