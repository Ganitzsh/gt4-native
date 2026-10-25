/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): the PS2 MMI version of Speex's fixed-point
 * fir_mem_up(x, a, y, N, M, mem, stack), written from its instructions. xx (M + N - 1 halfwords)
 * is the stack pointer rounded up to 16.
 * 1. xx[2i] = x[N/2-1-i] >> 15, written 8 halfwords at a time with sq (the odd halfwords get the
 *    low 16 bits of x[...] << 1), (N/2) / 4 times (a do-while); then (N/2) & 3 single halfwords
 *    x[...] >> 15 go every 4 bytes from the start of the last quadword written (as the original
 *    does; N/2 is a multiple of 4 in the game);
 * 2. xx[N+i] = mem[i+1] (low halfword) for even i < M - 1;
 * 3. for each group of 4 outputs (N / 4 times) and each group of 4 taps (M / 4 times), with
 *    x0 = xx[N-4-i] first, x1 = xx[N-2+j-i] and the next x0 = xx[N+j-i]:
 *    y0 += a[j]*x1 + a[j+2]*x0', y1 += a[j+1]*x1 + a[j+3]*x0', y2 += a[j]*x0 + a[j+2]*x1,
 *    y3 += a[j+1]*x0 + a[j+3]*x1 (pmaddh, 32-bit wrapping), then y[i..i+3] = sums >> 1
 *    (arithmetic; the sums are shifted once, not each product as in Speex), stored with sq;
 * 4. mem[i+1] = xx[i] for even i < M - 1.
 * sq ignores the low 4 address bits, as on the EE (the buffers are aligned in the game). */
extern "C" void func_0056E2C0(int *x, void *a, void *y, int N, int M, void *mem, int stack)
{
    const short *coef = (const short *)a;
    char *xx = (char *)(stack + ((0 - stack) & 15));
    short *xs = (short *)xx;
    int half = N / 2;
    int i, k;

    /* 1 */
    {
        const int *px = x + half - 1;
        char *q = xx - 16;
        int groups = half / 4;
        do {
            unsigned int w[4];
            short *h;
            q += 16;
            w[0] = (unsigned int)px[0];
            w[1] = (unsigned int)px[-1];
            w[2] = (unsigned int)px[-2];
            w[3] = (unsigned int)px[-3];
            h = (short *)((unsigned int)(q + 7) & ~15u);
            for (k = 0; k < 4; k++) {
                unsigned int s = w[k] << 1;
                h[2 * k] = (short)(s >> 16);
                h[2 * k + 1] = (short)(s & 0xFFFF);
            }
            px -= 4;
        } while (--groups != 0);
        for (k = half & 3; k != 0; k--) {
            *(short *)q = (short)(*px >> 15);
            px--;
            q += 4;
        }
    }

    /* 2 */
    for (i = 0; i < M - 1; i += 2)
        xs[N + i] = (short)((const int *)mem)[i + 1];

    /* 3 */
    {
        const short *px0 = xs + N - 4;
        int *out = (int *)y;
        int groups = N / 4;
        do {
            const short *pa = coef;
            const short *px = px0 + 2;
            int s0 = 0, s1 = 0, s2 = 0, s3 = 0;
            short x0 = *px0;
            int taps = M / 4;
            do {
                short a0 = pa[0], a1 = pa[1], a2 = pa[2], a3 = pa[3];
                short x1 = px[0];
                short xn = px[2];
                s0 = (int)((unsigned int)s0 + (unsigned int)(a0 * x1) + (unsigned int)(a2 * xn));
                s1 = (int)((unsigned int)s1 + (unsigned int)(a1 * x1) + (unsigned int)(a3 * xn));
                s2 = (int)((unsigned int)s2 + (unsigned int)(a0 * x0) + (unsigned int)(a2 * x1));
                s3 = (int)((unsigned int)s3 + (unsigned int)(a1 * x0) + (unsigned int)(a3 * x1));
                x0 = xn;
                pa += 4;
                px += 4;
            } while (--taps != 0);
            int *o = (int *)((unsigned int)out & ~15u);
            o[0] = s0 >> 1;
            o[1] = s1 >> 1;
            o[2] = s2 >> 1;
            o[3] = s3 >> 1;
            out += 4;
            px0 -= 4;
        } while (--groups != 0);
    }

    /* 4 */
    for (i = 0; i < M - 1; i += 2)
        ((int *)mem)[i + 1] = xs[i];
}
