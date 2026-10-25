/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of the provisional draft drafts/00448b48/00448b48_1f1f9709a74bce98.cpp (argument widths, argcheck.py width/retwidth): SPEC_DATABASE__DatabaseTable__getRow (00449298) takes (s32, s32, s32); the draft declared the row s64 (8 bytes natively), which moved the output pointer one slot. */
typedef long s64;
struct Src { s64 a0,a8,a10,a18,a20,a28,a30,a38; unsigned char b40; };
struct Dst { s64 a0,a8,a10,a18,a20,a28; unsigned char b30; s64 a38,a40; };
extern "C" int D_00623800, D_00623804;
extern "C" bool func_00449298(int, int, Src*);
static inline s64 low(s64 k) { return (int)(k & 0xFFFFFFFFUL); }
extern "C" bool func_00448B48(s64 key, Dst *out) {
    Src s;
    int hi = key >> 32;
    int lo = (int)(key & 0xFFFFFFFFUL);
    bool r;
    if (hi == 0) r = func_00449298(D_00623800, lo, &s);
    else if (hi == 0x26) r = func_00449298(D_00623804, lo, &s);
    else return 0;
    if (r)
    {
        s64 a=s.a0,b=s.a8,c=s.a10,d=s.a18,e=s.a20,f=s.a28;
        unsigned char v=s.b40;
        s64 g=s.a30,h=s.a38;
        out->a0=a; out->a8=b; out->a10=c; out->a18=d; out->a20=e;
        out->a28=f; out->b30=v; out->a38=g; out->a40=h;
        return 1;
    }
    return r;
}
