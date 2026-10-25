/* native stand-in, not matched: MPEG motion compensation of one chroma block (called twice by
   func_00568A38, for Cb and Cr, with halved vectors). Picks the 8x8 block of the reference window
   (rows of 16 bytes, position wrapped to 8) at (x + dx/2, y + dy/2), makes the half-pel prediction
   (dx & 1 horizontal, dy & 1 vertical: (a+b+1)>>1, or (a+b+c+d+2)>>2 for both) and stores it in
   dst (rows of 8 bytes), or averages it with dst ((p+d+1)>>1) when avg is set. The original does it
   with MMI 128-bit arithmetic, two rows per quadword for the full-pel and vertical cases; this is
   the same arithmetic per byte, from the assembly. */

void func_005692C0(unsigned char *dst, unsigned char *src, int x, int y, int dx, int dy, int avg)
{
    int hx, hy, r, i, p;

    src += ((y + (dy >> 1)) & 7) << 4;
    src += (x + (dx >> 1)) & 7;
    hx = dx & 1;
    hy = dy & 1;
    for (r = 0; r < 8; r++, src += 16, dst += 8) {
        for (i = 0; i < 8; i++) {
            if (hx && hy)
                p = (src[i] + src[i + 1] + src[i + 16] + src[i + 17] + 2) >> 2;
            else if (hx)
                p = (src[i] + src[i + 1] + 1) >> 1;
            else if (hy)
                p = (src[i] + src[i + 16] + 1) >> 1;
            else
                p = src[i];
            if (avg)
                p = (p + dst[i] + 1) >> 1;
            dst[i] = (unsigned char)p;
        }
    }
}
