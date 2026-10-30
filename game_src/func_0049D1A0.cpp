/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 0049D1A0 (from its matched source): an item's lighting. Kinds 1 and 4 (with a light change,
 * +0x48 bits 0-2, or `n` 2) rebuild the light constants (0049EA80, 0049EB90), which leave a pending block at +0x30:
 * a DMA ref tag to quadwords below the packet stack top with the VIF code that unpacks them (V4-32) into the
 * batch. When `n` is not 0 and a block is pending it goes into the batch (n - 1 is returned); and with a key-frame
 * count of 1 or 2 (kind >> 5) the blend weight (+0xC30) goes to lane w of qword 242. The original put the tag in
 * the packet, and the weight as STMASK 3F3F3F3F, UNPACK S-32 masked, STMASK back (+0x40); natively both are batch
 * writes (compat/game/gt4_gfx.h). */
#include "gt4_gfx.h"
typedef unsigned int u32;
typedef unsigned int u128 __attribute__((mode(TI)));

union func_0049D1A0_QW {
    u128 q;
    u32 w[4];
};

struct func_0049D1A0_Cmd {
    int pad0;
    float value;
    unsigned char kind;
};

struct func_0049D1A0_Obj {
    char pad0[0x30];
    func_0049D1A0_QW tag;
    u32 x40;
    char pad1[0x48 - 0x44];
    unsigned short x48;
    char pad2[0x1F8 - 0x4A];
    u32 *packet;
    char pad3[0xC30 - 0x1FC];
    u32 xc30;
};

extern "C" {
void func_0049EB90(func_0049D1A0_Obj *self, float v);
void func_0049EA80(func_0049D1A0_Obj *self, float v);
}

extern "C" int func_0049D1A0(func_0049D1A0_Obj *self, func_0049D1A0_Cmd *cmd, int n) {
    unsigned int count = cmd->kind >> 5;
    switch (cmd->kind & 0x1F) {
    case 4:
        if (self->x48 & 7)
            n = 2;
        if (n == 2)
            func_0049EB90(self, cmd->value);
        break;
    case 1:
        if (self->x48 & 7)
            n = 2;
        if (n == 2)
            func_0049EA80(self, cmd->value);
        break;
    case 0:
    case 2:
    case 3:
    case 5:
    case 6:
        break;
    }
    if (n != 0 && self->tag.w[0] != 0) {
        n--;
        /* the pending block: ref tag (qwords, address), VIF code UNPACK V4-32 (count, batch qword) */
        const u32 code = self->tag.w[3];
        int qwords = (code >> 16) & 0xFF;
        if (!qwords) qwords = 256;
        gt4_gfx_batch_write(self, code & 0x3FF, (const void *)(self->tag.w[1] & 0x0FFFFFFF), qwords, 0xF);
    }
    switch ((int)count) {
    case 1:
    case 2: {
        u32 weight[4];
        weight[0] = weight[1] = weight[2] = weight[3] = self->xc30;
        gt4_gfx_batch_write(self, 0xF2, weight, 1, 0x8);
        }
    }
    return n;
}
