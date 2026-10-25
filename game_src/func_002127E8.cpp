/* compiler: ee-gcc2.96-no-strict-aliasing */
// Native correction of the provisional draft of 002127E8 (drafts/002127e8/002127e8_6798ab1309f29cc1.cpp):
// func_005D8BE8 is map::insert_unique(value), which returns pair<iterator, bool> (8 bytes) through
// its first argument. The draft gave it a 4-byte Iter, so the bool overwrote the next local (the
// pair's key string) on the native stack; on the EE the frame left a gap there.
typedef unsigned int u32;
typedef int s32;
typedef unsigned int size_t;
struct Heap { const char *name; };
extern "C" Heap *func_005C11A8(void);
extern "C" void func_00326798(void *ptr, size_t size, size_t align, const char *name);
extern "C" size_t func_0057F260(const char *s) throw();
struct Rep;
extern "C" char *func_005C2560(Rep *r);
struct traits {
    static size_t length(const char *s) { return func_0057F260(s); }
};
struct Rep {
    size_t len, res, ref;
    bool selfish;
    char *data() { return reinterpret_cast<char *>(this + 1); }
    char *grab() { if (selfish) return clone(); ++ref; return data(); }
    void release() { if (--ref == 0) delete this; }
    inline static void operator delete(void *);
    char *clone() { return func_005C2560(this); }
};
extern Rep D_00659FA8;
struct String;
extern "C" String *func_005C2630(String *str, size_t pos, size_t n1, const char *s, size_t n2);
struct String {
    char *dat;
    Rep *rep() const { return reinterpret_cast<Rep *>(dat) - 1; }
    String &replace(size_t pos, size_t n1, const char *s, size_t n2) { return *func_005C2630(this, pos, n1, s, n2); }
    String &assign(const char *s, size_t n) { return replace(0, (size_t)-1, s, n); }
    String &assign(const char *s) { return assign(s, traits::length(s)); }
    String(): dat(D_00659FA8.grab()) {}
    String(const String &str): dat(str.rep()->grab()) {}
    String(const char *s, size_t n): dat(D_00659FA8.grab()) { assign(s, n); }
    String(const char *s): dat(D_00659FA8.grab()) { assign(s); }
    ~String() { rep()->release(); }
    String &operator=(const String &str) {
        if (&str != this) { rep()->release(); dat = str.rep()->grab(); }
        return *this;
    }
    String &operator=(const char *s) { return assign(s); }
    s32 length() const { return rep()->len; }
};
struct GameAlloc {
    static void deallocate(void *p, size_t n) { func_00326798(p, n, 4, func_005C11A8()->name); }
};
inline void Rep::operator delete(void *ptr) {
    GameAlloc::deallocate(ptr, sizeof(Rep) + reinterpret_cast<Rep *>(ptr)->res);
}


struct VEnt { short delta; short pad; void (*fn)(void *, String *); };
struct VT { char pad[0x190]; VEnt e; };
struct Obj { int a; VT *vt; };
struct H;
extern "C" void func_002556B0(H *);
extern "C" H *func_002550A0(H *, const H *);
extern "C" void func_002550B8(H *, s32);
extern "C" s32 func_0030ADA0(Obj *, H *, const void *, s32, const void *);
extern "C" const char *func_00305570(Obj *);
extern "C" const char *func_00305570_single(Obj *) asm("func_00305570");
extern char D_0082B4E0[];
struct H { Obj *p; H() { func_002556B0(this); } H(const H &o) { func_002550A0(this,&o); } ~H() { func_002550B8(this,2); } };
struct Pair { String key; H val; Pair(const String &s,const H &h):key(s),val(h) {} };
struct Iter { s32 p; s32 inserted; };
extern "C" void func_005D8BE8(Iter *, void *, const Pair *);

inline void *operator new(size_t, void *p) { return p; }
extern "C" void func_002127E8(void *arg0, Obj **arg1) {
    H h;
    char sbuf[0x10];
    Iter it;
    char pbuf[sizeof(Pair)];
    func_0030ADA0(*arg1,&h,D_0082B4E0,0,0);
    if (__builtin_expect((int)h.p, 0)) {
        String *ps = (String*)sbuf;
        VEnt *e = &h.p->vt->e;
        void *p = (char*)h.p + e->delta;
        const char *n = func_00305570(*arg1);
        new (ps) String(n);
        e->fn(p,ps);
        ps->~String();
        Pair *pair = (Pair*)pbuf;
        void *map = (char*)arg0+0x10;
        const char *key = func_00305570_single(h.p);
        new (ps) String(key);
        new (pair) Pair(*ps,h);
        func_005D8BE8(&it,map,pair);
        func_002550B8((H*)(pbuf+4),2);
        pair->key.~String();
        ps->~String();
    }
}
