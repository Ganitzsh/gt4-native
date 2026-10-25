/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): the PS2 MMI version of Speex's fixed-point
 * iir_mem2(x, den, y, N, ord, mem), written from its instructions. Per sample:
 * xi = clamp(x[i], -0x30000000, 0x30000000), yi = clamp(xi + (mem[0] << 2), same), nyi = -yi,
 * mem[j] = mem[j+1] + Q15(den[j+1], nyi) for j < ord - 1 (four at a time with pmulth on the 15-bit
 * halves of nyi, which equals the exact (c * v) >> 15; those four go out with sq, which ignores
 * the low 4 address bits, so mem is expected 16-aligned; then one by one),
 * mem[ord-1] = Q15(den[ord], nyi), y[i] = yi; sums wrap at 32 bits. */
static inline int clamp_sig(int v)
{
    if (v < (int)0xD0000000) v = (int)0xD0000000;
    if (v > 0x30000000) v = 0x30000000;
    return v;
}

static inline int q15(short c, int v) { return (int)(((long long)c * v) >> 15); }

extern "C" void func_0056E170(int *x, short *den, int *y, int N, int ord, int *mem)
{
    int groups = (ord - 1) / 4;
    int rest = (ord - 1) & 3;
    do {
        int xi = clamp_sig(*x++);
        int yi = clamp_sig((int)((unsigned int)xi + ((unsigned int)mem[0] << 2)));
        int nyi = (int)(0u - (unsigned int)yi);
        const short *pd = den + 1;
        int *pm = mem;
        int g, k;
        for (g = groups; g != 0; g--) {
            int out[4];
            int *q = (int *)((unsigned int)pm & ~15u);
            for (k = 0; k < 4; k++)
                out[k] = (int)((unsigned int)pm[1 + k] + (unsigned int)q15(pd[k], nyi));
            for (k = 0; k < 4; k++)
                q[k] = out[k];
            pd += 4;
            pm += 4;
        }
        for (k = rest; k != 0; k--) {
            *pm = (int)((unsigned int)pm[1] + (unsigned int)q15(*pd, nyi));
            pd++;
            pm++;
        }
        *pm = q15(*pd, nyi);
        *y++ = yi;
    } while (--N != 0);
}
