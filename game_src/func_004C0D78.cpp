/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 004C0D78 (unmatched; from the draft drafts/004c0d78/004c0d78_8a57cd0dc4c805f2.cpp, corrected
 * for the native calling convention): finds the slot for key in table id with 004C35E8 (a binary search),
 * takes a free slot when key is not there and stores it with 004C1118.
 * 004C35E8 is (Obj *base, u8 idx, unsigned key); the draft passed only (base, idx). The original's jal at
 * 0x4C0DDC sets $a0 and $a1 and leaves its own $a2 (key, unmasked) for the key; natively the key came
 * from the stack past the two arguments, garbage. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
struct E {
    char pad[0x1C];
    u32 *arr;
    char pad20[4];
    u16 *max;
    u16 *cur;
};
struct Obj {
    char pad0[0xC];
    u32 *p0C;
    char pad10[0x14];
    char *t;
};
extern "C" void func_004C1118(Obj *, int, int, int, u32, int);
extern "C" u16 func_004C35E8(char *, int, int);
extern "C" u16 func_004C3850(Obj *, int, void *, u16);

extern "C" int func_004C0D78(Obj *o, u8 id, int key) {
    int sp0 = (*o->p0C >> 12) & 0xFFFF;
    u32 s5 = key & 0xFFFFF;
    u32 fp = (*o->p0C << 20) | s5;
    u16 s0 = func_004C35E8(o->t, id, key);
    u16 a3 = s0;
    E *e = (E *)(o->t + id * 0x2C);
    char *g = o->t + id * 0x2C + 0x20;
    if (!(s0 < *e->cur && (e->arr[s0] & 0xFFFFF) == s5)) {
        u16 *cur = *(u16 **)(g + 8);
        u16 *max = *(u16 **)(g + 4);
        if (*cur != *max) {
            a3 = *cur;
            *cur = a3 + 1;
        } else {
            a3 = func_004C3850(o, id, g, a3);
            E *e2 = (E *)(o->t + id * 0x2C);
            if (s0 < **(u16 **)(o->t + id * 0x2C + 0x20 + 8)) {
                if (a3 < s0 && (key & 0xFFFFF) < (e2->arr[s0] & 0xFFFFF)) s0 = s0 - 1;
            } else {
                s0 = s0 - 1;
            }
        }
    }
    func_004C1118(o, id, s0, a3, fp, sp0);
    return 0;
}
