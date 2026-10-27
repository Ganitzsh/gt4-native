/* compiler: ee-gcc2.96 */
/* Native code for 004A4F48 (unmatched; from the decomp's draft, corrected against the original): a GS
 * texture upload as a DMA chain in the scratchpad's packet area. A CNT tag sends the BITBLTBUF/TRX setup
 * that func_004A4CF0 writes and the first IMAGE GIF tag; then, per chunk of at most 0x7FF8 quadwords, a
 * REF tag sends the pixels from `src` and a NEXT tag (linked later) continues the chain, with another CNT
 * tag and IMAGE GIF tag (EOP on the last) for the chunks after the first.
 * The draft wrote the REF tag's QWC/ID word into the GIF tag's slot (then overwritten), leaving the REF
 * tag's first word stale: the chain sent garbage to the GS (GT mode's map, which alone uploads this way:
 * giant translucent polygons, thousands of FRAME buffers). */
#include "spr.h"
typedef unsigned int u32;
typedef unsigned long u64;
struct Scratch { char pad[0x1fc]; char *top; char pad200[8]; u32 *link; char pad20c[0x1030 - 0x20c]; int total; };
extern "C" int func_004A4CF0(char *, u32 *, int, int, int, int) throw();

extern "C" void func_004A4F48(u32 x, int y, int z, int a3, int a4, int a5, int a6, u32 src) {
    Scratch *s = (Scratch *)spr_base();
    u32 packed = ((x >> 6) & 0xffff) | (((z >> 6) & 0xff) << 16) | (y << 24);
    char *top = s->top;
    char *p = top - 0x90;
    int n = func_004A4CF0(top - 0x80, &packed, a3, a4, a5, a6);
    if (n == 0) return;
    s->top = p;
    s->total += n;
    *s->link = (u32)p & 0x0fffffff;
    *(u32 *)p = 0x10000006;   // CNT: the setup and the first IMAGE tag
    p = top - 0x30;
    do {
        int chunk = n < 0x7ff9 ? n : 0x7ff8;
        n -= chunk;
        // GIF tag: IMAGE, NLOOP = chunk, EOP on the last chunk
        ((u64 *)p)[0] = n ? (u64)chunk | (0x8000UL << 44) : (u64)chunk | 0x0800000000008000UL;
        ((u64 *)p)[1] = 0;
        p += 16;
        ((u32 *)p)[0] = chunk | 0x30000000;      // REF: QWC = chunk
        ((u32 *)p)[1] = src & 0x0fffffff;        // ... from src
        p += 16;
        *(u32 *)p = 0x20000000;                  // NEXT, its address linked by the next packet
        src += chunk << 4;
        s->link = (u32 *)(p + 4);
        if (n) {
            s->top -= 0x40;
            p = s->top;
            *s->link = (u32)p & 0x0fffffff;
            *(u32 *)p = 0x10000001;              // CNT: the next IMAGE tag
            p += 16;
        }
    } while (n);
}
