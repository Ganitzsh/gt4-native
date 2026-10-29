/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001C1850 (matched with MMI asm): a byte mask of an image, 0xFF where the source byte is above
 * `value` (its low byte), else 0; rows padded to 16 bytes. Returns the last 16 bytes written, as the EE code leaves
 * them in its result register (0x01 in every byte when there is no row: its loop never ran). */
typedef unsigned int u128 __attribute__((mode(TI)));
extern "C" void *memcpy(void *, const void *, unsigned int);
extern "C" u128 func_001C1850(u128 *dst, u128 *src, int width, int height, int value) {
    const unsigned char threshold = (unsigned char)value;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    const int bytes = ((width + 15) & ~15) * height;
    unsigned char last[16];
    for (int k = 0; k < 16; ++k) last[k] = 1;
    for (int i = 0; i < bytes; i += 16) {
        for (int k = 0; k < 16; ++k) last[k] = s[i + k] > threshold ? 0xFF : 0;
        memcpy(d + i, last, 16);
    }
    u128 r;
    memcpy(&r, last, 16);
    return r;
}
