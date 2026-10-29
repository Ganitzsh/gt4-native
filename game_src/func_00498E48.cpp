/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00498E48 (from its draft, checked against the original): the MMI colour blend in plain C. */
struct Frame { float time; unsigned int *data; };
struct Obj { char pad[4]; unsigned short count, count2; unsigned int *out; Frame *frames; unsigned short first, second; float time, blend; short other; char flag; unsigned char dirty; };

/* Blends two frames' colours by blend: each byte becomes bits 12..19 of first*n + second*(4096 - n), n = blend*4096
   truncated. The original widens the bytes to words (PEXTLB, PEXTLH), multiplies them by the factors' low halfwords
   (PMULTH, PMADDH), shifts (PSRLW 12) and packs the low bytes (PPACH, PPACB). */
extern "C" void func_00498E48(Obj *o) {
    if (o->dirty) {
        int n = (int)(o->blend * 4096.0f);
        const unsigned int *p = o->frames[o->first].data;
        const unsigned int *q = o->frames[o->second].data;
        unsigned int *out = o->out;
        int a = (short)n, b = (short)(4096 - n);
        for (int i = 0; i < o->count; i++) {
            unsigned int x = p[i], y = q[i], r = 0;
            for (int k = 0; k < 32; k += 8)
                r |= ((unsigned int)((int)(x >> k & 0xff) * a + (int)(y >> k & 0xff) * b) >> 12 & 0xff) << k;
            out[i] = r;
        }
        o->dirty = 0;
    }
}
