/* compiler: ee-gcc2.96-no-strict-aliasing */
// Native correction of the draft drafts/00328dd0/00328dd0_e8dcb4a5016d53a6.cpp (no matched source): it declares func_0057FB50 (fptodp)
// as returning double, but the soft-float helper returns the double's bits as a 64-bit integer
// (edx:eax natively, as gt4-native implements it), so the double read from st(0) was garbage.
// Declared as returning the bits here and converted back to double through a union (bit-exact).
typedef unsigned int u32;
struct Chunk { int x0, x4; Chunk *next; int use; int x10; void *start; void *end; };
struct Pool { char pad[0x30]; Chunk *head; void *tail; void *free; int count; u32 bytes, total, used, maxuse; };
extern "C" void func_0030C378(void *, const char *, ...);
extern "C" int *func_0030C430(void *);
extern "C" int *func_0030C448(void *);
extern "C" u32 func_00328A38(Pool *) throw();
extern "C" long func_0057FB50(float); /* the double's bits */
static inline double bits_to_double(long l) { union { long l; double d; } u; u.l = l; return u.d; }
extern char D_0069ECD8[], D_0069ECF0[], D_0069ED00[], D_0069ED28[], D_0069ED40[], D_0069ED58[], D_0069ED60[], D_0069ED78[], D_0069ED98[], D_0069EDE0[], D_0069EE28[], D_0069EE38[];
static inline u32 align_size(u32 n, u32 align) {
    if (n & (align - 1)) n = (n & ~(align - 1)) + align;
    return n;
}
extern "C" void func_00328DD0(Pool *self, void *out) {
    int chunks = 0;
    func_0030C378(out, D_0069ECD8, self->bytes, self->total);
    for (Chunk *c = self->head; c != 0; c = c->next) {
        func_0030C378(out, D_0069ECF0, chunks);
        func_0030C430(out);
        u32 total = self->total;
        u32 size = (self->bytes + 4) * total + 0x1C;
        ++chunks;
        u32 aligned = align_size(size, 16);
        func_0030C378(out, D_0069ED00, c, (char *)c - 16, size, aligned + 16);
        func_0030C378(out, D_0069ED28, c->start, c->end);
        func_0030C378(out, D_0069ED40, c->use, total);
        func_0030C448(out);
        func_0030C378(out, D_0069ED58);
    }
    func_0030C378(out, D_0069ED60, self->count, chunks * self->total);
    const char *fmt = D_0069ED78;
    u32 total = self->total;
    u32 maxuse = self->maxuse;
    double pct = 0.0;
    if (total) pct = bits_to_double(func_0057FB50(((float)maxuse / (float)total) * 100.0f));
    func_0030C378(out, fmt, maxuse, total, pct);
    if (self->total != 0 && self->maxuse >= self->total) {
        func_0030C378(out, D_0069ED98);
        func_0030C378(out, D_0069EDE0);
        func_0030C378(out, D_0069ED98);
        func_0030C378(out, D_0069EE28);
    }
    func_0030C378(out, D_0069EE38, self->bytes, func_00328A38(self));
}
