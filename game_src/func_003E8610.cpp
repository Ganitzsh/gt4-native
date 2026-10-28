/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 003E8610 (unmatched; from the decomp's draft drafts/003e8610/003e8610_68262787242dea22.cpp,
 * corrected for the native calling convention): draws one rotated particle quad (four corners around the
 * item's position, rotated by the record's angle and scaled by its size) through func_003E8438, with the
 * colour/alpha word computed by func_003E8BA8.
 * The draft declared func_003E8BA8 (Self *, Arg *) with one parameter: the original's jal at 0x3E8644 sets
 * nothing new and leaves $a0 = self and $a1 = it, its own incoming arguments, so the callee reads it->depth
 * and it->idx. Natively the second argument was the stack slot past self, garbage. */
typedef unsigned char u8;
struct Rec {
    char pad0[2];
    u8 b;
    char pad3[5];
    float scale;
    char padC[8];
    float angle;
    char pad18[0x28];
};
struct Item { float x, y, z; int index; };
struct Self { char *recs; };
extern char D_006A3AB0[];
extern "C" int func_003E8BA8(Self *, Item *);
extern "C" void func_004A5F00(float *, float *, float);
extern "C" void func_003E8438(Self *, void *, void *, int, int, int) throw();

extern "C" void func_003E8610(Self *self, Item *it) {
    float pts[12];
    float s, c;
    Rec *r = (Rec *)(self->recs + (it->index << 6));
    u8 b = r->b;
    int v = func_003E8BA8(self, it);
    func_004A5F00(&s, &c, r->angle);
    float sc = r->scale;
    float x = it->x, y = it->y, z = it->z;
    float t0 = (c - s) * sc;
    float t2 = (s + c) * sc;
    pts[0] = x + t0;
    pts[1] = y + t2;
    pts[2] = z;
    pts[3] = x - t2;
    pts[4] = y + t0;
    pts[5] = z;
    pts[6] = x + t2;
    pts[7] = y - t0;
    pts[8] = z;
    pts[9] = x - t0;
    pts[10] = y - t2;
    pts[11] = z;
    func_003E8438(self, pts, D_006A3AB0 + ((b >> 6) << 5), v, v, 4);
}
