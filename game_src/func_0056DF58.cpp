/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): the PS2 MMI version of Speex's fixed-point
 * filter_mem2(x, num, den, y, N, ord, mem), written from its instructions. Per sample:
 * xi = clamp(x[i], -0x30000000, 0x30000000), yi = clamp(xi + (mem[0] << 2), same), then
 * mem[j] = mem[j+1] + Q15(num[j+1], xi) + Q15(den[j+1], -yi) for j < ord - 1 (four at a time with
 * pmulth on the 15-bit halves, then one by one; both equal (c * v) >> 15 exactly),
 * mem[ord-1] = Q15(num[ord], xi) - Q15(den[ord], yi) (note: yi, not -yi), y[i] = yi; Q15(c, v) is
 * the 64-bit product shifted right by 15 (arithmetic), sums wrap at 32 bits. */
static inline int clamp_sig(int v)
{
    if (v < (int)0xD0000000) v = (int)0xD0000000;
    if (v > 0x30000000) v = 0x30000000;
    return v;
}

static inline int q15(short c, int v) { return (int)(((long long)c * v) >> 15); }

extern "C" void func_0056DF58(int *x, short *num, short *den, int *y, int N, int ord, int *mem)
{
    int groups = (ord - 1) / 4;
    int rest = (ord - 1) & 3;
    do {
        int xi = clamp_sig(*x++);
        int yi = clamp_sig((int)((unsigned int)xi + ((unsigned int)mem[0] << 2)));
        int nyi = (int)(0u - (unsigned int)yi);
        const short *pn = num + 1;
        const short *pd = den + 1;
        int *pm = mem;
        int g, k;
        for (g = groups; g != 0; g--) {
            int out[4];
            for (k = 0; k < 4; k++)
                out[k] = (int)((unsigned int)pm[1 + k] + (unsigned int)q15(pn[k], xi)
                               + (unsigned int)q15(pd[k], nyi));
            for (k = 0; k < 4; k++)
                pm[k] = out[k];
            pn += 4;
            pd += 4;
            pm += 4;
        }
        for (k = rest; k != 0; k--) {
            *pm = (int)((unsigned int)pm[1] + (unsigned int)q15(*pn, xi) + (unsigned int)q15(*pd, nyi));
            pn++;
            pd++;
            pm++;
        }
        *pm = (int)((unsigned int)q15(*pn, xi) - (unsigned int)q15(*pd, yi));
        *y++ = yi;
    } while (--N != 0);
}
