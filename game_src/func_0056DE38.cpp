/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): the PS2 MMI version of Speex's fixed-point compute_rms(x, len),
 * written from its instructions, quirks included:
 * - peak: the largest |x[i]| (pabsw, saturating) over len / 4 quadwords, at least 1; shift =
 *   18 - lz when lz < 18, else 0, where lz = the leading zeros after the sign bit (plzcw);
 * - energy: per quadword only x[4i] and x[4i+1] are used (the second pextlw repeats the first
 *   pair), shifted right by `shift`; pmaddh accumulates the squares of their low halfwords
 *   twice (lanes 0 and 2), so the sums are S0 = 2 * sum(x0^2) and S1 = 2 * sum(x1^2) (paddsw,
 *   saturating); the original then takes the high word of the 64-bit value (S1:S0) + S1 shifted
 *   right by 38, which is S1 >> 6;
 * - result: ((func_0056E958(energy / len + 1) << (shift + 3)) << 2) >> 16 (spx_sqrt).
 * The loops are do-whiles over len / 4 quadwords (len is a positive multiple of 4); the peak loop
 * reads aligned quadwords (lq), the energy loop plain words. */
extern "C" int func_0056E958(int x);

static inline int sat_add(int a, int b)
{
    long long v = (long long)a + b;
    return v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (int)(-2147483647 - 1) : (int)v;
}

static inline int lz_after_sign(int x)
{
    unsigned int u = x < 0 ? ~(unsigned int)x : (unsigned int)x;
    int n = 0;
    while (n < 31 && !(u & (0x40000000u >> n)))
        n++;
    return n;
}

extern "C" int func_0056DE38(int *x, int len)
{
    int groups = len / 4;
    int peak[4] = { 1, 1, 1, 1 };
    const int *p = (const int *)((unsigned int)x & ~15u);   /* lq ignores the low 4 bits */
    int n = groups;
    do {
        for (int k = 0; k < 4; k++) {
            int v = p[k];
            int a = v == (int)0x80000000 ? 0x7FFFFFFF : v < 0 ? -v : v;
            if (a > peak[k]) peak[k] = a;
        }
        p += 4;
    } while (--n != 0);
    int m02 = peak[0] > peak[2] ? peak[0] : peak[2];
    int m13 = peak[1] > peak[3] ? peak[1] : peak[3];
    int m = m13 > m02 ? m13 : m02;
    int lz = lz_after_sign(m);
    int shift = lz < 18 ? 18 - lz : 0;

    int lo0 = 0, lo2 = 0, hi0 = 0, hi2 = 0;   /* the LO/HI lanes that reach the result */
    p = x;
    n = groups;
    do {
        short a = (short)(p[0] >> shift);
        short b = (short)(p[1] >> shift);
        lo0 = (int)((unsigned int)lo0 + (unsigned int)(a * a));
        hi0 = (int)((unsigned int)hi0 + (unsigned int)(b * b));
        lo2 = (int)((unsigned int)lo2 + (unsigned int)(a * a));
        hi2 = (int)((unsigned int)hi2 + (unsigned int)(b * b));
        p += 4;
    } while (--n != 0);
    unsigned int s0 = (unsigned int)sat_add(lo0, lo2);
    int s1 = sat_add(hi0, hi2);
    long long v = (long long)(((unsigned long long)(unsigned int)s1 << 32) | s0) + (unsigned int)s1;
    int energy = (int)(v >> 38);

    int r = func_0056E958(energy / len + 1);
    r = (int)((unsigned int)r << ((shift + 3) & 31));
    return (int)((unsigned int)r << 2) >> 16;
}
