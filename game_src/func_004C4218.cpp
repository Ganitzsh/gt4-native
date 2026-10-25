/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/004c4218/004c4218_51fa0b85f7230126.cpp (argument widths, argcheck.py width/retwidth): `count` is used as 32-bit only and its caller (InitWnn) passes an int; declared long (8 bytes natively) it read info and size one slot off. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
struct Info { u32 type; u32 x4; u16 x8; u16 n; u8 xC[0x18]; u32 table[1]; };
struct Sub10 { u16 kind; u16 pad; u8 *p14; u8 *p18; u8 *pc; };
struct Sub20 { u32 *slot; u16 *pn; u16 *p8; };
struct Entry { u8 flag; u8 pad[3]; u8 *text; u8 *data; u32 size; Sub10 s10; Sub20 s20; };
struct Tbl { Entry e[4]; u8 count; };
struct Ctx { char pad[0x24]; Tbl *tbl; };
extern Ctx *D_006452D4;
extern "C" void func_004C14A0(Tbl *, int, int) throw();
extern "C" void func_004C16C8(Tbl *, int) throw();
extern "C" void func_004C4B98(u32, void *);
static inline Ctx **context() { return &D_006452D4; }
extern "C" int func_004C4218(u8 *hdr, int count, Info *info, int size) {
    int n;
    int type;
    u16 entries;
    int i;
    int h = ((hdr[0] << 24) & 0xFF000000) | hdr[3];
    int l = (hdr[2] << 8) | ((hdr[1] << 16) & 0xFF0000);
    if ((h | l) != 1) return 0x80004457;
    h = ((hdr[0x18] << 24) & 0xFF000000) | hdr[0x1B];
    l = (hdr[0x1A] << 8) | ((hdr[0x19] << 16) & 0xFF0000);
    n = h | l;
    if (n == 0) return 0x80004457;
    if (n != (long)(int)count) return 0x80004457;
    u8 *q = (u8 *)info;
    h = ((q[0] << 24) & 0xFF000000) | q[3];
    l = (q[2] << 8) | ((q[1] << 16) & 0xFF0000);
    type = h | l;
    if (type != 0 && type != (int)0xE0000000) return 0x80004457;
    if (size < 12) return 0x80004457;
    entries = (u32)(size - 12) / 6;
    if (entries < 7) return 0x80004457;
    entries -= 6;
    if (!info) return 0x80004457;
    if (type == (int)0xE0000000 && entries != info->n) return 0x80004457;
    for (i = 0; i < 1; i++) {
        if (!(*context())->tbl->e[i].text) break;
    }
    if (i == 1) return 0x80004557;
    u8 *text = hdr + ((((hdr[8] << 24) & 0xFF000000) | hdr[0xB]) | ((hdr[0xA] << 8) | ((hdr[9] << 16) & 0xFF0000)));
    u8 *data = hdr + ((((hdr[0xC] << 24) & 0xFF000000) | hdr[0xF]) | ((hdr[0xE] << 8) | ((hdr[0xD] << 16) & 0xFF0000)));
    u32 sz = (hdr[4] << 24) | hdr[7] | ((hdr[6] << 8) | (hdr[5] << 16));
    u8 *p14 = hdr + ((((hdr[0x10] << 24) & 0xFF000000) | hdr[0x13]) | ((hdr[0x12] << 8) | ((hdr[0x11] << 16) & 0xFF0000)));
    u8 *p18 = hdr + ((((hdr[0x14] << 24) & 0xFF000000) | hdr[0x17]) | ((hdr[0x16] << 8) | ((hdr[0x15] << 16) & 0xFF0000)));
    (*context())->tbl->e[i].flag = 0;
    (*context())->tbl->e[i].text = text;
    (*context())->tbl->e[i].data = data;
    (*context())->tbl->e[i].size = sz;
    (*context())->tbl->e[i].s10.kind = 4;
    (*context())->tbl->e[i].s10.p14 = p14;
    (*context())->tbl->e[i].s10.p18 = p18;
    (*context())->tbl->e[i].s20.p8 = &info->x8;
    (*context())->tbl->e[i].s10.pc = info->xC;
    (*context())->tbl->e[i].s20.pn = &info->n;
    if (type == 0) *(*context())->tbl->e[i].s20.pn = entries;
    (*context())->tbl->e[i].s20.slot = &info->table[*(*context())->tbl->e[i].s20.pn];
    if (type == 0) {
        func_004C16C8((*context())->tbl, i);
        func_004C4B98(0xE0000000, info);
    } else {
        func_004C14A0((*context())->tbl, i, 1);
    }
    (*context())->tbl->count++;
    return i;
}
