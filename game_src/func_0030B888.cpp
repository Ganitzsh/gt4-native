/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction (width): func_00326750 (the allocator) takes unsigned int size, int align, const char *name; this source declares the size long (one register on the EE, 8 bytes natively), which shifts align and name. Declaring it unsigned int no longer matches on the EE (the long is truncated). */
typedef int s32;
extern const char D_0069DE48[];
struct Vec { s32 pad0; s32 *begin; s32 *end; s32 *cap; };
struct Obj { char pad[0xC]; Vec *vec; };
extern "C" void func_00309378(void *, s32) throw();
extern "C" void *func_0030BA08(void *, s32);
extern "C" int func_0030BB18(void *);
extern "C" void *func_00326750(unsigned int, int, const char *);
extern "C" void func_003285A8(s32);
extern "C" void func_003285F8(s32);
extern "C" const char **HObject__tf(void);
extern "C" s32 *func_005D84B0(s32 *, s32, void *, s32);

static inline void assign(s32 *dst, s32 *val) {
            if (dst != val) {
                s32 nv = val[0];
                if (nv != 0)
                    func_003285A8(nv);
                if (*dst != 0)
                    func_003285F8(*dst);
                *dst = nv;
            }
}

extern "C" void hObject__deepCopy(Obj *self, Obj *src) {
    if (src != 0 && src->vec != 0) {
        s32 pad[4];
        s32 it[4];
        s32 buf[4];
        s32 n = (src->vec->end - src->vec->begin);
        s32 i = 0;
        Vec *v = (Vec *)func_00326750(0x100, 4, D_0069DE48);
        s32 bytes = n * 4;
        v->begin = 0;
        v->end = 0;
        v->cap = 0;
        s32 *mem = (s32 *)func_00326750(bytes, 4, *HObject__tf());
        v->begin = mem;
        v->cap = (s32 *)((char *)mem + bytes);
        v->end = mem;
        it[0] = (s32)mem;
        {
            func_0030BB18(buf);
            v->end = func_005D84B0((s32 *)it[0], n, buf, 0);
            func_00309378(buf, 2);
        }
        self->vec = v;
        for (; i < n;) {
            s32 off = i * 4;
            i++;
            s32 *dst = (s32 *)((char *)self->vec->begin + off);
            func_0030BA08(buf, *(s32 *)((char *)src->vec->begin + off));
            assign(dst, buf);
            func_00309378(buf, 2);
        }
    }
}
