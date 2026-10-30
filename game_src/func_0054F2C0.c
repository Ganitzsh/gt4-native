/* compiler: ee-gcc2.96-nsa-nosib-as2004 */
/* Native version of 0054F2C0 (from its source): memcpy by unaligned 8-byte words, then the remaining bytes; without
   the prefetch hint (pref) in its word loop. */
typedef unsigned long long u64;
typedef struct { u64 v; } __attribute__((packed)) U64;

static inline void copy8(U64 *d, const U64 *s) { d->v = s->v; }

void func_0054F2C0(void *dst, const void *src, unsigned int n) {
    U64 *d = dst;
    const U64 *s = src;
    U64 *e = d + (n >> 3);
    char *dc;
    const char *sc;
    char *ec;

    while (d != e) {
        copy8(d++, s++);
    }
    dc = (char *)d;
    sc = (const char *)s;
    ec = dc + (n & 7);
    while (dc != ec) *dc++ = *sc++;
}
