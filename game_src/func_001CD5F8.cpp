/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native copy of the provisional draft for 001CD5F8: the first slot-0xC8 virtual gets src and out, which the EE forwards to it in $a1 and $a2 (passthrough_lint) */
typedef int s32;
typedef unsigned char u8;
typedef unsigned long u64;

typedef long s64;

/* bits 32-48: the index of the file */
struct FileId {
    s64 bits;
    s32 index() const { return (s32)(bits >> 32) & 0x1FFFF; }
    void setIndex(s32 v) { bits = (bits & 0xFFFE0000FFFFFFFFUL) | (0x1FFFF00000000UL & ((u64)v << 32)); }
};

static inline void setBit(u8 *set, unsigned int n) { set[n >> 3] |= 1 << (n & 7); }

struct Rec { char pad[0x1C]; char key[0x24]; };

struct Source {
    virtual void v0();
    virtual void v1();
    virtual void v2();
    virtual void v3();
    virtual s32 failed();
    virtual void v5();
    virtual void v6();
    virtual void v7();
    virtual void v8();
    virtual void v9();
    virtual void v10();
    virtual void v11();
    virtual s32 list(char *path, Rec *out, s32 start, s32 count);
};

struct VE0 { short delta; short idx; s32 (*fn)(void *); };
struct Self { char pad[0x4C]; char *vtbl; };

extern "C" void func_001CD580(char *, s32) throw();
extern "C" void *func_005A48D8(void *, s32, unsigned int);
extern "C" FileId func_001CBEB0(void *) throw();
extern "C" void func_001CD8A0(FileId, s32, char *);

static inline s32 vcallC8(Self *o) {
    VE0 *e = (VE0 *)(o->vtbl + 0xC8);
    return e->fn((char *)o + e->delta);
}

static inline s32 vcallC8_fwd(Self *o, void *a1, void *a2) {
    VE0 *e = (VE0 *)(o->vtbl + 0xC8);
    return ((s32 (*)(void *, void *, void *))e->fn)((char *)o + e->delta, a1, a2);
}

extern "C" s32 func_001CD5F8(Self *self, Source *src, FileId *out) {
    char path[0x30];
    u8 used[0x7D];
    Rec recs[16];
    s32 start = 0;
    s32 n;
    s32 id = 0;
    func_001CD580(path, vcallC8_fwd(self, src, out));
    func_005A48D8(used, 0, sizeof(used));
    used[0x7C] = 0;
    do {
        n = src->list(path, recs, start, 0x10);
        if (src->failed() != 0) return 0;
        for (s32 i = 0; i < n; i++) {
            s32 id = func_001CBEB0(recs[i].key).index();
            if (id < 999) setBit(used, id);
        }
        start += n;
    } while (n >= 0x10);
    s32 a = 0;
    while (used[a] == 0xFF) a++;
    s32 v = used[a];
    a <<= 3;
    while (v & 1) {
        v >>= 1;
        a++;
    }
    id = a;
    if (id >= 999) id = 0;
    out->setIndex(id);
    func_001CD8A0(*out, vcallC8(self), (char *)self + 0x24);
    return 1;
}
