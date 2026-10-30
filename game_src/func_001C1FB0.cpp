/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 001C1FB0 (from its source): copies n bytes (a multiple of 128) in 128-byte blocks, quadwords
   loaded before they are stored, without the prefetch hint (pref). */
typedef unsigned int u128 __attribute__((mode(TI)));
extern "C" void func_001C1FB0(char *dst, char *src, int n) {
    char *end = dst + n;
    while (dst != end) {
        u128 a = ((u128 *)src)[0];
        u128 b = ((u128 *)src)[1];
        u128 c = ((u128 *)src)[2];
        u128 d = ((u128 *)src)[3];
        u128 e = ((u128 *)src)[4];
        u128 f = ((u128 *)src)[5];
        u128 g = ((u128 *)src)[6];
        u128 h = ((u128 *)src)[7];
        ((u128 *)dst)[0] = a;
        ((u128 *)dst)[1] = b;
        ((u128 *)dst)[2] = c;
        ((u128 *)dst)[3] = d;
        ((u128 *)dst)[4] = e;
        ((u128 *)dst)[5] = f;
        ((u128 *)dst)[6] = g;
        ((u128 *)dst)[7] = h;
        dst += 128;
        src += 128;
    }
}
