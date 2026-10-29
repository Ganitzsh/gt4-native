/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0056BA50 (from its draft, checked against the original): the MMI difference loop in plain C. */

static inline int saturate(long long v) { return v > 0x7fffffffLL ? 0x7fffffff : v < -0x80000000LL ? (int)0x80000000 : (int)v; }

/* For n/4 quads (at least one: the count is tested after each), each word of a minus b, rounded (+0xFFF) and scaled
   (>> 13, arithmetic), clamped to +-0x7FFF, stored as a halfword: PSUBSW, PADDSW (saturating), PSRAW, PMAXW/PMINW,
   PPACH. LQ ignores the addresses' low four bits. */
extern "C" void func_0056BA50(void *a, void *b, void *out, int n) {
    const int *x = (const int *)((unsigned int)a & ~15u);
    const int *y = (const int *)((unsigned int)b & ~15u);
    short *o = (short *)out;
    n /= 4;
    do {
        n--;
        for (int k = 0; k < 4; k++) {
            int v = saturate(saturate((long long)x[k] - y[k]) + 0xfffLL) >> 13;
            if (v < -0x7fff) v = -0x7fff;
            if (v > 0x7fff) v = 0x7fff;
            o[k] = (short)v;
        }
        x += 4;
        y += 4;
        o += 4;
    } while (n != 0);
}
