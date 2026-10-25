/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native correction of src/engine/unit_0046A050/func_00474A40.cpp: func_00605778 takes its comparison object
 * by reference (const Matcher &). ee-gcc 2.96 passes this Cmp argument by invisible reference (a
 * pointer to a copy in $a3), so the matched by-value declaration works on the EE; natively the
 * 8-byte struct would be copied onto the stack where the callee reads a pointer. */
typedef int s32;
struct Ent { s32 a; s32 b; s32 c; };
struct Arr {
    Ent *data;
    s32 n;
    Ent *begin() { return data; }
    Ent *end() { return data + n; }
};
struct Obj { char pad[0x10]; Arr arr; };
struct Cmp {
    char c;
    s32 len;
    Cmp(s32 l) : len(l) {}
};
extern "C" s32 func_00474980(const char *, s32, Ent *) throw();
extern "C" s32 func_0057F260(const char *) throw();
extern "C" Ent *func_00605778(Ent *, Ent *, const char *const &, const Cmp &, s32 *); /* the callee takes the comparison object by reference */
static inline s32 *distance_type(Ent *const &) { return (s32 *)0; }
static inline Ent *lower_bound(Ent *first, Ent *last, const char *const &val, Cmp comp) {
    return func_00605778(first, last, val, Cmp(comp), distance_type(first));
}
extern "C" s32 func_00474A40(Obj *self, const char *key) {
    s32 len = func_0057F260(key);
    Ent *it = lower_bound(self->arr.begin(), self->arr.end(), key, Cmp(len));
    if (it == self->arr.end() || func_00474980(key, len, it) != 0) return -1;
    return it->a;
}
