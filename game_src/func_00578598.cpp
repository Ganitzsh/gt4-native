/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00578598 (from its draft, checked against the original): a random float from the generator
   00579330, its PLZCW in plain C. */
extern "C" unsigned func_00579330(unsigned *);

/* v = the generator's next value; nonzero, it becomes a float's bits: exponent e = 125 - k, where k is PLZCW(v) & 31
   (the copies of the sign bit after bit 31) or -1 when v is negative; mantissa v << (-e & 31) (SLLV takes the
   amount's low five bits), >> 9. */
extern "C" float func_00578598(unsigned *p) {
    union { unsigned i; float f; } r;
    int v = func_00579330(p);
    if (v != 0) {
        int k;
        if (v < 0) {
            k = -1;
        } else {
            unsigned x = (unsigned)v;
            k = 0;
            while (!(x & 0x40000000u)) { x <<= 1; k++; }
        }
        int exp = 125 - k;
        v = (exp << 23) | (((unsigned)v << (-exp & 31)) >> 9);
    }
    r.i = v;
    return r.f;
}
