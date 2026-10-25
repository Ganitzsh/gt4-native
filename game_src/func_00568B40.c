/* native stand-in, not matched: MPEG motion compensation of a luma macroblock (called by
   func_00568A38 with the scratchpad prediction buffer and reference window). Picks the 16x16 block
   of the reference window (rows of 32 bytes, position wrapped to 16) at (x + dx/2, y + dy/2), makes
   the half-pel prediction (dx & 1 horizontal, dy & 1 vertical: (a+b+1)>>1, or (a+b+c+d+2)>>2 for
   both) and stores it in dst (rows of 16 bytes), or averages it with dst ((p+d+1)>>1) when avg is
   set. The original does it with MMI 128-bit arithmetic (pextlb/paddh/pcgth/psrlh/ppacb) and
   unaligned ldl/ldr loads; this is the same arithmetic per byte, from the assembly. */

void func_00568B40(unsigned char *dst, unsigned char *src, int x, int y, int dx, int dy, int avg)
{
    int hx, hy, r, i, p;

    src += ((y + (dy >> 1)) & 15) << 5;
    src += (x + (dx >> 1)) & 15;
    hx = dx & 1;
    hy = dy & 1;
    for (r = 0; r < 16; r++, src += 32, dst += 16) {
        for (i = 0; i < 16; i++) {
            if (hx && hy)
                p = (src[i] + src[i + 1] + src[i + 32] + src[i + 33] + 2) >> 2;
            else if (hx)
                p = (src[i] + src[i + 1] + 1) >> 1;
            else if (hy)
                p = (src[i] + src[i + 32] + 1) >> 1;
            else
                p = src[i];
            if (avg)
                p = (p + dst[i] + 1) >> 1;
            dst[i] = (unsigned char)p;
        }
    }
}
