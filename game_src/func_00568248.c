/* native stand-in, not matched: the MPEG video decoder's IPU block decode (called per macroblock by
   the slice loop func_00563F98). Returns 2 when the macroblock is neither intra nor has a pattern;
   otherwise sets IPU_CTRL (picture type, intra DC precision...), starts DMA channel 3 (fromIPU) into
   req->blocks for 0x30 quadwords, issues a BDEC command (MBI, DCR, DT, QSC) and waits for it,
   feeding the bitstream (func_0054D888) when the FIFO runs dry, then waits for the DMA (bc0f) and
   returns 1 on an IPU error (CTRL bit 15), 3 on start code (bit 14), else 0.
   Plain C from the assembly: the registers are plain memory natively, so real decoding needs a
   runtime IPU model (as for the matched IPU functions around it); the bc0f DMA wait is dropped. */

struct BlockRequest {
    unsigned char *blocks;      /* 0x00 */
    int macroblock_type;        /* 0x04 */
    int picture_coding_type;    /* 0x08 */
    int coded_block_pattern;    /* 0x0C */
    int quantizer_scale;        /* 0x10 */
    int intra_dc_precision;     /* 0x14 -> IPU_CTRL bit 20 (IVF?) */
    int x18;                    /* 0x18 -> IPU_CTRL bits 16-17 */
    int x1c;                    /* 0x1C -> IPU_CTRL bit 21 */
    int x20;                    /* 0x20 -> IPU_CTRL bit 22 */
    int reset_dc_predictors;    /* 0x24 -> BDEC DCR */
};

#define REG(a) (*(volatile unsigned int *)(a))
#define IPU_CMD    REG(0x10002000)
#define IPU_CTRL   REG(0x10002010)
#define IPU_BP     REG(0x10002020)
#define D3_CHCR    REG(0x1000B000)
#define D3_MADR    REG(0x1000B010)
#define D3_QWC     REG(0x1000B020)
#define D_ENABLER  REG(0x1000F520)
#define D_ENABLEW  REG(0x1000F590)

void func_0054D5C8(void);
void func_0054D888(void);
int func_005B72A8(void);    /* DIntr */
int func_005B72F8(void);    /* EIntr */

int func_00568248(struct BlockRequest *req)
{
    int intra = req->macroblock_type & 1;
    unsigned int ctrl, cmd, enabler;
    int n;

    if (!intra && !(req->macroblock_type & 2))
        return 2;

    ctrl = (req->picture_coding_type << 24) & 0x07000000;
    if (req->intra_dc_precision)
        ctrl |= 0x100000;
    ctrl |= (req->x18 << 16) & 0x30000;
    if (req->x1c)
        ctrl |= 0x200000;
    if (req->x20)
        ctrl |= 0x400000;
    func_0054D5C8();
    IPU_CTRL = ctrl;

    cmd = intra ? 0x28000000 : 0x20000000;          /* BDEC, MBI */
    if (req->reset_dc_predictors)
        cmd |= 0x04000000;                         /* DCR */
    cmd |= (req->quantizer_scale << 16) & 0x1F0000; /* QSC */
    if (req->coded_block_pattern)
        cmd |= 0x02000000;                         /* DT */

    D3_MADR = ((unsigned int)req->blocks & 0x0FFFFFFF) | 0x80000000;
    D3_QWC = 0x30;
    while ((int)IPU_CTRL < 0)
        ;
    func_005B72A8();
    enabler = D_ENABLER;
    D_ENABLEW = enabler | 0x10000;
    D3_CHCR |= 0x100;
    D_ENABLEW = enabler & 0xFFFEFFFF;
    func_005B72F8();

    IPU_CMD = cmd;
    n = 0x3D0900;
    if ((int)IPU_CTRL < 0) {
        n = 0x3D08FF;
        do {
            if (((IPU_BP >> 8) & 0xF) == 0)
                func_0054D888();
            if ((int)IPU_CTRL >= 0)
                break;
        } while (--n > 0);
    }
    /* original: bc0f loop (DMA channel 3 done) */

    ctrl = IPU_CTRL;
    if (ctrl & 0x8000)
        return 1;
    return (ctrl & 0x4000) ? 3 : 0;
}
