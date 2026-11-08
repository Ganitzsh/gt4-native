/* compiler: ee-gcc2.96-nogcse */
/* DEBUG (never push): BillboardSet__render from gt4-decomp, with a debug Light per billboard (A7 lamp search). */
#include "gt4_gfx.h"
#include "spr.h"
struct Ent {
    unsigned short id;
    unsigned short type;
    float f4, f8, fC;
};
struct Group {
    unsigned short n;
    unsigned short pad;
    Ent e[1];
};
struct Item {
    float f0, f4;
    int f8, fC;
};
struct ItemTab { Item it[1]; };
struct Obj {
    char pad0[0x10];
    unsigned short count;
    char pad12[6];
    Group **groups;
    char pad1C[8];
    ItemTab *items;
    int *tab;
};

extern float D_00846590[];

extern "C" {
void func_0049BBB0(int, int, int, int, int);
void func_0049BBF0();
int func_0049C0D8(int, float *, int, int, int, int);
void func_0049CEA0(int);
int func_0049D308(int);
float *func_004A5B70(int);
}

static inline Item *item_at(Obj *o, unsigned n) { ItemTab *b = o->items; return b->it + n; }

extern "C" void BillboardSet__render(Obj *o, int idx) {
    if (o == 0) return;
    if (idx >= o->count) return;
    gt4_debug_billboards(spr_base(), o, idx);
    Group *g = o->groups[idx];
    if (g == 0) return;
    Ent *e = g->e;
    unsigned fp = 0;
    int i = 0;
    int open = 0;
    if (g->n != 0) {
        do {
            unsigned t = e->id;
            if (t & 0x8000) {
                if (open != 0) {
                    open = 0;
                    func_0049BBF0();
                }
                fp = 0;
                t &= 0xFFFF7FFF;
                func_0049D308(o->tab[t]);
            } else {
                Item *it = item_at(o, t);
                if (fp != e->type) {
                    if (open != 0) {
                        open = 0;
                        func_0049BBF0();
                    }
                    func_0049CEA0(e->type);
                    fp = e->type;
                }
                float *v = func_004A5B70(3);
                float t6 = e->f4 + D_00846590[0] * it->f0;
                v[9] = t6;
                float t7 = e->f8;
                v[10] = t7;
                float t5 = e->fC + D_00846590[2] * it->f0;
                v[11] = t5;
                float t3 = e->f4 - D_00846590[0] * it->f0;
                v[3] = t3;
                float t4 = e->f8;
                v[4] = t4;
                float t2 = e->fC - D_00846590[2] * it->f0;
                v[5] = t2;
                v[6] = t6;
                v[7] = t7 + it->f4;
                v[8] = t5;
                v[0] = t3;
                v[1] = t4 + it->f4;
                v[2] = t2;
                if (open == 0) {
                    open = 1;
                    func_0049BBB0(4, 0x32, 0x31, 0x27, 0x32);
                }
                func_0049C0D8(4, v, it->f8, it->fC, 0, 0);
            }
            i++;
            e++;
        } while (i < g->n);
    }
    if (open != 0) {
        func_0049BBF0();
    }
}
