/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native stand-in (not matched): a blocking scratchpad DMA copy of size bytes (QWC = size / 16)
 * from src to dst. When dst is a scratchpad address (0x7xxxxxxx) it starts the toSPR channel (9:
 * SADR = dst & 0x3FFF, MADR = src & 0x0FFFFFFF), else the fromSPR channel (8: SADR = src & 0x3FFF,
 * MADR = dst & 0x0FFFFFFF). skip != 0 selects interleave mode (D_SQWC: TQWC = block / 16,
 * SQWC = skip / 16): the main-memory side moves block bytes, then skips skip bytes. Unless bit 0
 * of flags is set it writes the data cache back first (FlushCache(0)); it then starts the channel
 * with DMA suspended around the start, waits on CPCOND0 for the end and clears the channel's
 * D_PCR bit; returns 0. Natively the DMA registers are plain memory and nothing runs them, so the
 * copy is done here (the scratchpad side wraps at 16 KB, like SADR); the cache flush has no
 * meaning natively. */
typedef unsigned int u32;

static unsigned char *spr_at(u32 sadr) { return (unsigned char *)(0x70000000u + (sadr & 0x3FF0u)); }
static unsigned char *ram_at(u32 madr) { return (unsigned char *)((madr & 0x0FFFFFFFu) & ~15u); }

int func_00562820(u32 dst, u32 src, u32 size, u32 block, u32 skip, u32 flags)
{
    int to_spr = (dst & 0x70000000u) == 0x70000000u;
    u32 sadr = to_spr ? dst & 0x3FFFu : src & 0x3FFFu;
    u32 madr = to_spr ? src : dst;
    u32 qwc = (size >> 4) & 0xFFFFu;
    u32 tqwc = (block >> 4) & 0xFFu;
    u32 sqwc = (skip >> 4) & 0xFFu;
    u32 in_block = 0;
    int k;
    (void)flags;
    while (qwc != 0) {
        unsigned char *s = spr_at(sadr);
        unsigned char *m = ram_at(madr);
        if (to_spr)
            for (k = 0; k < 16; k++) s[k] = m[k];
        else
            for (k = 0; k < 16; k++) m[k] = s[k];
        sadr += 16;
        madr += 16;
        qwc--;
        if (skip != 0 && ++in_block == tqwc) {
            madr += sqwc << 4;
            in_block = 0;
        }
    }
    return 0;
}
