/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of src/engine/unit_00450248/func_00454C58.cpp (argument order, argcheck.py orders): func_00498F88 is (obj, int id, float dt) natively, as its draft and its other caller func_004504C8 declare it; the decomp's (obj, float, int) is the same EE call but matches only in that order. */
struct S {
    char pad[0x1c];
    unsigned short cols, rows;
    char p[0x24];
    void ***grid;
};
extern "C" void func_00498F88(void *, int, float);

extern "C" void func_00454C58(S *self, float t, int arg)
{
    unsigned short rows = self->rows;
    int row = 0;
    if (rows) {
        do {
            unsigned off = row * 4;
            if (*(void ***)(off + (unsigned)self->grid)) {
                int col = 0;
                if (self->cols) {
                    do {
                        unsigned inner = col * 4;
                        ++col;
                        func_00498F88(*(void **)(inner + (unsigned)*(void ***)(off + (unsigned)self->grid)), arg, t);
                    } while (col < self->cols);
                    rows = self->rows;
                }
            }
            ++row;
        } while (row < rows);
    }
}
