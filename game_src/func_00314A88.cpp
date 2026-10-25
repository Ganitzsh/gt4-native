// Native correction of src/adhoc/hString/hString__toFloat.cpp: the matched source is void and
// leaves the value in $f0, where dptofp (0057FA48) put it and where the callers (the script
// method, through the vtable; include/gt4/hString.h declares f32) read it. Natively nothing is
// returned in st(0). Return dptofp's float (natively a float in st(0), as the draft linked for
// 0057FA48 returns it). On the EE `return dptofp(...)` becomes a tail call (j) where the original
// has jal + jr, so the matched source keeps the void form.
typedef unsigned int u32;
extern char D_0069E1B0[];
#define EMPTY D_0069E1B0

struct String {
    struct Rep { u32 len, res, ref, selfish; char *data() { return (char *)(this + 1); } };
    char *dat;
    Rep *rep() const { return (Rep *)dat - 1; }
    u32 length() const { return rep()->len; }
    const char *data() const { return dat; }
    void terminate() const { rep()->data()[length()] = 0; }
};

static inline const char *c_str(const String &s) {
    if (s.length() == 0)
        return EMPTY;
    s.terminate();
    return s.data();
}

struct Obj { char pad[0x10]; String name; };
extern "C" double func_005A2F70(const char *);
extern "C" float dptofp(double);

extern "C" float hString__toFloat(Obj *o) {
    return dptofp(func_005A2F70(c_str(o->name)));
}
