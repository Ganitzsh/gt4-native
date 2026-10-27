/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native code for 00458340 (unmatched; from the decomp's draft, corrected against the original): if the
 * owner is valid (p->self == p), walks a rows x cols grid of Entry pointers (rows starting at p + 0x20,
 * each row cols * 4 bytes on from the previous) and returns 0 if any non-null entry has start + size >= end
 * (unsigned), else 1; returns 0 when p->self != p.
 * The draft read rows and cols as 32-bit ints at 0x10 and 0x14; the original reads two unsigned 16-bit
 * halves at 0x10 and 0x12 (lhu $v0, 0x10($a0) at 0x45834C, lhu $t3, 0x12($a0) at 0x458358), so the draft's
 * row count included cols in its upper half and its column count came from an unrelated word. */
struct Entry { unsigned int start, size; };
struct Owner { int pad; Owner *self; char pad8[8]; unsigned short rows, cols; char pad14[12]; Entry *entries[1]; };
extern "C" int func_00458340(Owner *p, unsigned int end) {
    if (p != p->self) return 0;
    int flag = 0;
    int rows = p->rows;
    if (rows) {
        int cols = p->cols;
        char *row = (char *)p;
        int i = rows;
        do {
            Entry **cur = (Entry **)(row + 0x20);
            if (cols) {
                int j = cols;
                do {
                    Entry *e = *cur++;
                    if (e) flag = (e->start + e->size >= end) || flag;
                    j--;
                } while (j);
            }
            i--;
            row += cols * 4;
        } while (i);
    }
    return flag ^ 1;
}
