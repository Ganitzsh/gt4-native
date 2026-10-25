/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef unsigned long u64;
extern "C" void func_0049FFA0(void *) throw();
extern "C" void func_004A0080(void *) throw();
extern "C" void func_004A0160(void *) throw();
extern "C" void func_004A0228(void *) throw();
extern "C" int func_004AA460(void *);
struct Obj {
 char pad0[9]; char f9; char padA; char fb; char padC[8];
 int f14; char pad18[0x4a-0x18]; short f4a;
 char pad4c[0x120-0x4c]; u64 f120;
 char pad128[0x148-0x128]; u64 f148;
 char pad150[0x188-0x150]; u64 f188;
 char pad190[0x1d8-0x190]; u64 f1d8;
 char pad1e0[0x200-0x1e0]; int f200;
 char pad204[0x234-0x204]; int f234; float f238;
 char pad23c[0x2c8-0x23c]; int f2c8; int f2cc;
 char pad2d0[0xc00-0x2d0]; int fc00; int fc04; float fc08; int fc0c;
 char padc10[0xc20-0xc10]; int fc20; int fc24;
};
extern "C" int func_004A02A0(Obj *p, long unusedw0) {
    p->f120 |= 1UL;
    p->f148 |= 1UL;
    p->f188 &= 0xfffffffeffffffffUL;
    p->f1d8 = 0x03ff1fff1fffUL;
    p->f4a = 8;
    p->fc20 = -1;
    p->fc24 = 0x80808080;
    p->f238 = 1.0f;
    p->fc08 = 1.0f;
    p->f2c8 = 2;
    p->f9 = 1;
    p->f14 = 0x385;
    p->f200 = 0;
    p->f234 = 0;
    p->fc00 = 0;
    p->fc04 = 0;
    p->fc0c = 0;
    p->f2cc = 0;
    p->fb = 0;
    func_004A0228(p);
    func_0049FFA0(p);
    func_004A0080(p);
    func_004A0160(p);
    return func_004AA460(p);
}
/* native correction of src/network/unit_00494578/func_004A02A0.cpp: returns the value of the tail call func_004AA460 (chain to func_004AA3C0) */
