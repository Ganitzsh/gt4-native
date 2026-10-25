/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction (width): func_00326750 (the allocator) takes unsigned int size, int align, const char *name; this source declares the size long (one register on the EE, 8 bytes natively), which shifts align and name. Declaring it unsigned int no longer matches on the EE (the long is truncated). */
typedef int s32;
extern char D_0069DE48[];
struct Vec { s32 pad0; s32 *begin; s32 *end; s32 *cap; };
struct Obj { char pad[8]; s32 x8; Vec *vec; };
extern "C" void func_00309378(void *, s32) throw();
extern "C" void *func_00326750(unsigned int, int, const char *);
extern "C" void func_00326798(void *, s32, s32, const char *);
extern "C" s32 func_003284E0(void *);
extern "C" const char **HObject__tf(void);
extern "C" s32 *func_005EC5E0(s32 *, s32 *, s32 *, s32);

extern "C" Obj *func_0030A798(Obj *self, Obj *other) {
    s32 it[8];
    if (self != other) {
        func_003284E0(self);
        s32 x8 = other->x8;
        Vec *old = self->vec;
        self->x8 = x8;
        if (old) {
            it[0] = (s32)old->begin;
            s32 *p = (s32 *)it[0];
            s32 *e = old->end;
            if (p != e) {
                s32 *q = p;
                do {
                    p++;
                    func_00309378(q, 2);
                    q = p;
                } while (p != e);
            }
            s32 *b = old->begin;
            s32 sz = (old->cap - b);
            if (b)
                func_00326798(b, sz * 4, 4, *HObject__tf());
            func_00326798(old, 0x100, 4, D_0069DE48);
        }
        if (other->vec) {
            Vec *nv = (Vec *)func_00326750(0x100, 4, D_0069DE48);
            Vec *ov = other->vec;
            s32 bytes = (((s32)ov->end - (s32)ov->begin) >> 2) * 4;
            nv->begin = 0;
            nv->end = 0;
            nv->cap = 0;
            s32 *mem = (s32 *)func_00326750(bytes, 4, *HObject__tf());
            nv->begin = mem;
            nv->cap = (s32 *)((char *)mem + bytes);
            nv->end = mem;
            s32 *f = ov->begin;
            s32 *l = ov->end;
            it[0] = (s32)mem;
            nv->end = func_005EC5E0(f, l, mem, 0);
            self->vec = nv;
        } else {
            self->vec = 0;
        }
    }
    return self;
}
