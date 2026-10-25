/* compiler: ee-gcc2.9-991111 */
/* Native correction of the provisional draft build/swarm/a3/00545d7c_best.c (argument widths, argcheck.py width/retwidth): the shift is a 32-bit bit count (every caller passes u32/s32); declared u64 (8 bytes natively) the high half was garbage. */
typedef unsigned int u32;
typedef unsigned long u64;
void func_00545D7C(u32 *words, u32 shift)
{
    int bits = shift & 31;
    u32 skip = shift >> 5;
    int complement = 32 - bits;
    u32 n = 32;
    u32 *out = words + 32;
    u32 *src = out - skip;
    u64 previous;
    do {
        --src;
        --n;
        previous = *src;
        if (previous != 0) break;
    } while (n != skip);
    if (previous != 0) {
        u32 adjust = n < 31;
        n += adjust;
        src += adjust;
        previous = *src;
        out = src + skip + 1;
        while (n != skip) {
            u64 next, high;
            --src;
            --out;
            next = *src;
            high = previous;
            previous = next;
            --n;
            *out = (next >> complement) | (high << bits);
        }
    }
    --out;
    *out = previous << bits;
    while (n != 0) {
        --out;
        --n;
        *out = 0;
    }
}
