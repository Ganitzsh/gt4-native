/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched; the closest draft, an SGI vector<unsigned short> with a custom
 * allocator, is one register choice off): visits the indices 1..n-1 of a ring in a random order.
 * n first loses d->off when n >= d->cap; a vector of n - 1 halfwords (heap 0x40, func_00575E60,
 * zero-filled) gets v[k-1] = (int)(rand() * (float)(k + 1)) for k = n-1 down to 1 (rand is the
 * float in [0, 1) of func_00579548(rng); k + 1 converted as unsigned); then cb(ctx, i', v[i-1]')
 * for i = 1..n-1, where x' = x + d->off when x >= d->cap; the vector is freed (func_00575DA0). */
struct Desc { unsigned int cap; unsigned int off; };

extern "C" void *func_00575E60(int heap, int size);
extern "C" void func_00575DA0(int p);
extern "C" float func_00579548(int rng);

extern "C" void func_005D24A8(void *ctx, unsigned int n, void *rng,
                              void (*cb)(void *, unsigned int, unsigned int), struct Desc *d)
{
    unsigned short *v = 0;
    unsigned int count, k, i;
    if (n >= d->cap)
        n -= d->off;
    count = n - 1;
    if (count != 0) {
        v = (unsigned short *)func_00575E60(0x40, (int)(count * 2));
        for (k = 0; k < count; k++)
            v[k] = 0;
    }
    for (k = n - 1; k != 0; k--) {
        float r = func_00579548((int)rng);
        v[k - 1] = (unsigned short)(int)(r * (float)(k + 1));
    }
    for (i = 1; i < n; i++) {
        unsigned int a = i, b;
        if (a >= d->cap)
            a += d->off;
        b = v[i - 1];
        if (b >= d->cap)
            b += d->off;
        cb(ctx, a, b);
    }
    if (v != 0)
        func_00575DA0((int)v);
}
