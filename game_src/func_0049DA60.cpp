/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 0049DA60 (unmatched; from the decomp's worker draft build/swarm/a3/0049da60_best.cpp,
 * corrected for the native calling convention): picks the table entry t->base + t->idx * 16 and, by bit 0x20
 * of flags, tail-calls one of four packet builders (0049DCB0 / 0049DB08 with the entry's two bytes and float
 * scale, 0049D728 / 0049D580 with its two halfwords).
 * The draft declared func_0049DB08 (s32, void *, u8, u8, float); its definition (draft 0049db08) is
 * (Ctx *, u8 *cmd, int n, float scale): the original's j at 0x49DABC leaves $a2 = byte 4 (n) and
 * $f12 = the float at +0 (scale), and the callee never reads $a3 (byte 5, 0 on this path). Natively the
 * callee read the scale from the fourth slot, byte 5's int (0 -> 0.0f). Now the call passes (a0, q, b4, f). */
typedef int s32;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
struct Tbl { char *base; u16 idx; };
extern "C" s32 func_0049D580(s32, void *, u16, u16) throw();
extern "C" s32 func_0049D728(s32, void *, u16, u16) throw();
extern "C" s32 func_0049DB08(s32, void *, u8, float);
extern "C" s32 func_0049DCB0(s32, void *, u8, u8, float);
extern "C" s32 func_0049DA60(s32 a0, Tbl *t, u32 flags) {
    char *p = t->base + t->idx * 0x10;
    if (flags & 0x20) {
        u8 b5 = *(u8 *)(p + 5);
        void *q = p + 6;
        float f = *(float *)p;
        u8 b4 = *(u8 *)(p + 4);
        if (b5 != 0) return func_0049DCB0(a0, q, b4, b5, f);
        return func_0049DB08(a0, q, b4, f);
    }
    u16 h2 = *(u16 *)(p + 2);
    u16 h0 = *(u16 *)p;
    void *q2 = p + 4;
    if (h2 != 0) return func_0049D728(a0, q2, h0, h2);
    return func_0049D580(a0, q2, h0, h2);
}
